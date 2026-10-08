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
** WebAssembly port.
**
** A stand-in for the parts of the Win32 SDK that the game uses. Types and
** constants match the Windows SDK; functions are implemented on top of
** POSIX/Emscripten in Dependencies/WebCompat/src (or as no-ops where the browser
** has no equivalent).
*/
#pragma once
#ifndef WEBCOMPAT_WINDOWS_H
#define WEBCOMPAT_WINDOWS_H

#include "msvcrt_compat.h"

/* The game targets Windows 98 / 2000 and later. */
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
** Calling conventions and declaration decorations
** ------------------------------------------------------------------------- */
#ifndef WINAPI
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR
#define far
#define near
#define CONST const
#define VOID void
#define STDMETHODCALLTYPE
#define STDAPICALLTYPE
#define WINBASEAPI
#define WINUSERAPI
#define DECLSPEC_IMPORT
#endif

/* Calling conventions mean nothing on WebAssembly. */
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef _cdecl
#define _cdecl
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef _stdcall
#define _stdcall
#endif

#ifndef IN
#define IN
#define OUT
#define OPTIONAL
#endif

/* ---------------------------------------------------------------------------
** Basic types (ILP32, the same as 32-bit Windows)
** ------------------------------------------------------------------------- */
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned long       DWORD;
typedef unsigned int        UINT;
typedef int                 INT;
typedef long                LONG;
typedef unsigned long       ULONG;
typedef short               SHORT;
typedef unsigned short      USHORT;
typedef char                CHAR;
typedef unsigned char       UCHAR;
typedef wchar_t             WCHAR;
typedef float               FLOAT;
typedef double              DOUBLE;
typedef BYTE                BOOLEAN;
typedef long long           LONGLONG;
typedef unsigned long long  ULONGLONG;
typedef unsigned long long  DWORDLONG;
typedef long long           INT64;
typedef unsigned long long  UINT64;
typedef int                 INT32;
typedef unsigned int        UINT32;
typedef short               INT16;
typedef unsigned short      UINT16;
typedef signed char         INT8;
typedef unsigned char       UINT8;
typedef int                 LONG32;
typedef unsigned int        ULONG32;
typedef unsigned int        DWORD32;

typedef intptr_t            INT_PTR;
typedef uintptr_t           UINT_PTR;
typedef intptr_t            LONG_PTR;
typedef uintptr_t           ULONG_PTR;
typedef ULONG_PTR           DWORD_PTR;
typedef ULONG_PTR           SIZE_T;
typedef LONG_PTR            SSIZE_T;

#ifdef UNICODE
typedef WCHAR TCHAR;
#else
typedef char TCHAR;
#endif
typedef TCHAR               _TCHAR;
typedef unsigned char       TBYTE;

typedef void               *PVOID, *LPVOID;
typedef const void         *LPCVOID;
typedef BOOL               *PBOOL, *LPBOOL;
typedef BYTE               *PBYTE, *LPBYTE;
typedef WORD               *PWORD, *LPWORD;
typedef DWORD              *PDWORD, *LPDWORD;
typedef LONG               *PLONG, *LPLONG;
typedef ULONG              *PULONG;
typedef UINT               *PUINT, *LPUINT;
typedef INT                *PINT, *LPINT;
typedef float              *PFLOAT;
typedef CHAR               *PCHAR, *PSTR, *LPSTR, *NPSTR;
typedef const CHAR         *PCSTR, *LPCSTR;
typedef WCHAR              *PWCHAR, *PWSTR, *LPWSTR;
typedef const WCHAR        *PCWSTR, *LPCWSTR;
typedef TCHAR              *PTSTR, *LPTSTR;
typedef const TCHAR        *PCTSTR, *LPCTSTR;

typedef void               *HANDLE;
typedef HANDLE             *PHANDLE, *LPHANDLE;
#define DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__ *name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HIMC);
DECLARE_HANDLE(HDROP);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HGDIOBJ_);
typedef void               *HGDIOBJ;
typedef HICON               HCURSOR;
typedef HINSTANCE           HMODULE;
typedef HANDLE              HGLOBAL;
typedef HANDLE              HLOCAL;
typedef HANDLE              GLOBALHANDLE;
typedef HKEY               *PHKEY;
typedef int                 HFILE;

typedef LONG                HRESULT;
typedef LONG_PTR            LRESULT;
typedef UINT_PTR            WPARAM;
typedef LONG_PTR            LPARAM;
typedef WORD                ATOM;
typedef DWORD               COLORREF;
typedef DWORD              *LPCOLORREF;
typedef DWORD               LCID;
typedef WORD                LANGID;
typedef LONG                SCODE;
typedef DWORD               ACCESS_MASK;
typedef ACCESS_MASK         REGSAM;

typedef int (WINAPI *FARPROC)(void);
typedef int (WINAPI *NEARPROC)(void);
typedef int (WINAPI *PROC)(void);
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
typedef void (CALLBACK *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

/* ---------------------------------------------------------------------------
** Structures
** ------------------------------------------------------------------------- */
typedef union _LARGE_INTEGER {
	struct { DWORD LowPart; LONG HighPart; };
	struct { DWORD LowPart; LONG HighPart; } u;
	LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
	struct { DWORD LowPart; DWORD HighPart; };
	struct { DWORD LowPart; DWORD HighPart; } u;
	ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct tagPOINT { LONG x; LONG y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagPOINTS { SHORT x; SHORT y; } POINTS;
typedef struct tagSIZE { LONG cx; LONG cy; } SIZE, *PSIZE, *LPSIZE;
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct tagMSG {
	HWND   hwnd;
	UINT   message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD  time;
	POINT  pt;
} MSG, *PMSG, *LPMSG;

typedef struct _GUID {
	unsigned long  Data1;
	unsigned short Data2;
	unsigned short Data3;
	unsigned char  Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
typedef GUID *LPGUID;
typedef const GUID *LPCGUID;
typedef IID *LPIID;
typedef CLSID *LPCLSID;
#ifdef __cplusplus
#define REFGUID const GUID &
#define REFIID const IID &
#define REFCLSID const CLSID &
#else
#define REFGUID const GUID *
#define REFIID const IID *
#define REFCLSID const CLSID *
#endif
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	static const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

typedef struct _CRITICAL_SECTION {
	void *Impl;  /* a recursive pthread mutex, allocated on first use */
	LONG  LockCount;
	LONG  RecursionCount;
	HANDLE OwningThread;
	HANDLE LockSemaphore;
	ULONG_PTR SpinCount;
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

typedef struct _SECURITY_ATTRIBUTES {
	DWORD  nLength;
	LPVOID lpSecurityDescriptor;
	BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	DWORD Offset;
	DWORD OffsetHigh;
	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

#define MAX_PATH 260

typedef struct _WIN32_FIND_DATAA {
	DWORD    dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD    nFileSizeHigh;
	DWORD    nFileSizeLow;
	DWORD    dwReserved0;
	DWORD    dwReserved1;
	CHAR     cFileName[MAX_PATH];
	CHAR     cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *PWIN32_FIND_DATA, *LPWIN32_FIND_DATA, *LPWIN32_FIND_DATAA;

typedef struct _BY_HANDLE_FILE_INFORMATION {
	DWORD    dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD    dwVolumeSerialNumber;
	DWORD    nFileSizeHigh;
	DWORD    nFileSizeLow;
	DWORD    nNumberOfLinks;
	DWORD    nFileIndexHigh;
	DWORD    nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;

typedef struct _OSVERSIONINFOA {
	DWORD dwOSVersionInfoSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformId;
	CHAR  szCSDVersion[128];
} OSVERSIONINFOA, OSVERSIONINFO, *POSVERSIONINFO, *LPOSVERSIONINFO;

typedef struct _OSVERSIONINFOEXA {
	DWORD dwOSVersionInfoSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformId;
	CHAR  szCSDVersion[128];
	WORD  wServicePackMajor;
	WORD  wServicePackMinor;
	WORD  wSuiteMask;
	BYTE  wProductType;
	BYTE  wReserved;
} OSVERSIONINFOEX, *LPOSVERSIONINFOEX;

typedef struct _MEMORYSTATUS {
	DWORD  dwLength;
	DWORD  dwMemoryLoad;
	SIZE_T dwTotalPhys;
	SIZE_T dwAvailPhys;
	SIZE_T dwTotalPageFile;
	SIZE_T dwAvailPageFile;
	SIZE_T dwTotalVirtual;
	SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _SYSTEM_INFO {
	union {
		DWORD dwOemId;
		struct { WORD wProcessorArchitecture; WORD wReserved; };
	};
	DWORD     dwPageSize;
	LPVOID    lpMinimumApplicationAddress;
	LPVOID    lpMaximumApplicationAddress;
	DWORD_PTR dwActiveProcessorMask;
	DWORD     dwNumberOfProcessors;
	DWORD     dwProcessorType;
	DWORD     dwAllocationGranularity;
	WORD      wProcessorLevel;
	WORD      wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct tagRGBQUAD {
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD;

typedef struct tagPALETTEENTRY {
	BYTE peRed;
	BYTE peGreen;
	BYTE peBlue;
	BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
	WORD  bfType;
	DWORD bfSize;
	WORD  bfReserved1;
	WORD  bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPINFOHEADER {
	DWORD biSize;
	LONG  biWidth;
	LONG  biHeight;
	WORD  biPlanes;
	WORD  biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG  biXPelsPerMeter;
	LONG  biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct _RGNDATAHEADER {
	DWORD dwSize;
	DWORD iType;
	DWORD nCount;
	DWORD nRgnSize;
	RECT  rcBound;
} RGNDATAHEADER, *PRGNDATAHEADER;
typedef struct _RGNDATA {
	RGNDATAHEADER rdh;
	char          Buffer[1];
} RGNDATA, *PRGNDATA, *LPRGNDATA;

typedef struct _POINTFLOAT { FLOAT x; FLOAT y; } POINTFLOAT, *PPOINTFLOAT;
typedef struct _GLYPHMETRICSFLOAT {
	FLOAT      gmfBlackBoxX;
	FLOAT      gmfBlackBoxY;
	POINTFLOAT gmfptGlyphOrigin;
	FLOAT      gmfCellIncX;
	FLOAT      gmfCellIncY;
} GLYPHMETRICSFLOAT, *PGLYPHMETRICSFLOAT, *LPGLYPHMETRICSFLOAT;

typedef struct tagBITMAPINFO {
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD          bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO, *PBITMAPINFO;

typedef struct tagBITMAP {
	LONG   bmType;
	LONG   bmWidth;
	LONG   bmHeight;
	LONG   bmWidthBytes;
	WORD   bmPlanes;
	WORD   bmBitsPixel;
	LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

typedef struct tagTEXTMETRICA {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	CHAR tmFirstChar;
	CHAR tmLastChar;
	CHAR tmDefaultChar;
	CHAR tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
} TEXTMETRICA, TEXTMETRIC, *LPTEXTMETRIC;

typedef struct tagLOGFONTA {
	LONG lfHeight;
	LONG lfWidth;
	LONG lfEscapement;
	LONG lfOrientation;
	LONG lfWeight;
	BYTE lfItalic;
	BYTE lfUnderline;
	BYTE lfStrikeOut;
	BYTE lfCharSet;
	BYTE lfOutPrecision;
	BYTE lfClipPrecision;
	BYTE lfQuality;
	BYTE lfPitchAndFamily;
	CHAR lfFaceName[32];
} LOGFONTA, LOGFONT, *LPLOGFONT;

typedef struct tagWNDCLASSA {
	UINT      style;
	WNDPROC   lpfnWndProc;
	int       cbClsExtra;
	int       cbWndExtra;
	HINSTANCE hInstance;
	HICON     hIcon;
	HCURSOR   hCursor;
	HBRUSH    hbrBackground;
	LPCSTR    lpszMenuName;
	LPCSTR    lpszClassName;
} WNDCLASSA, WNDCLASS, *LPWNDCLASS;

typedef struct tagPAINTSTRUCT {
	HDC  hdc;
	BOOL fErase;
	RECT rcPaint;
	BOOL fRestore;
	BOOL fIncUpdate;
	BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct tagWINDOWPLACEMENT {
	UINT  length;
	UINT  flags;
	UINT  showCmd;
	POINT ptMinPosition;
	POINT ptMaxPosition;
	RECT  rcNormalPosition;
} WINDOWPLACEMENT;

typedef struct _STARTUPINFOA {
	DWORD  cb;
	LPSTR  lpReserved;
	LPSTR  lpDesktop;
	LPSTR  lpTitle;
	DWORD  dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
	WORD   wShowWindow;
	WORD   cbReserved2;
	LPBYTE lpReserved2;
	HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOA, STARTUPINFO, *LPSTARTUPINFO;

typedef struct _PROCESS_INFORMATION {
	HANDLE hProcess;
	HANDLE hThread;
	DWORD  dwProcessId;
	DWORD  dwThreadId;
} PROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _TIME_ZONE_INFORMATION {
	LONG       Bias;
	WCHAR      StandardName[32];
	SYSTEMTIME StandardDate;
	LONG       StandardBias;
	WCHAR      DaylightName[32];
	SYSTEMTIME DaylightDate;
	LONG       DaylightBias;
} TIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

/* x86 thread context, used only by the crash reporter. */
typedef struct _FLOATING_SAVE_AREA {
	DWORD ControlWord, StatusWord, TagWord, ErrorOffset, ErrorSelector, DataOffset, DataSelector;
	BYTE  RegisterArea[80];
	DWORD Cr0NpxState;
} FLOATING_SAVE_AREA;
typedef struct _CONTEXT {
	DWORD ContextFlags;
	DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
	FLOATING_SAVE_AREA FloatSave;
	DWORD SegGs, SegFs, SegEs, SegDs;
	DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
	DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
	BYTE  ExtendedRegisters[512];
} CONTEXT, *PCONTEXT, *LPCONTEXT;
#define CONTEXT_FULL 0x10007
#define CONTEXT_CONTROL 0x10001
#define CONTEXT_INTEGER 0x10002

typedef struct _EXCEPTION_RECORD {
	DWORD ExceptionCode;
	DWORD ExceptionFlags;
	struct _EXCEPTION_RECORD *ExceptionRecord;
	PVOID ExceptionAddress;
	DWORD NumberParameters;
	ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _EXCEPTION_POINTERS {
	PEXCEPTION_RECORD ExceptionRecord;
	PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG (WINAPI *LPTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *);

#define EXCEPTION_EXECUTE_HANDLER       1
#define EXCEPTION_CONTINUE_SEARCH       0
#define EXCEPTION_CONTINUE_EXECUTION    (-1)
#define EXCEPTION_ACCESS_VIOLATION         0xC0000005L
#define EXCEPTION_DATATYPE_MISALIGNMENT    0x80000002L
#define EXCEPTION_BREAKPOINT               0x80000003L
#define EXCEPTION_SINGLE_STEP              0x80000004L
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED    0xC000008CL
#define EXCEPTION_FLT_DENORMAL_OPERAND     0xC000008DL
#define EXCEPTION_FLT_DIVIDE_BY_ZERO       0xC000008EL
#define EXCEPTION_FLT_INEXACT_RESULT       0xC000008FL
#define EXCEPTION_FLT_INVALID_OPERATION    0xC0000090L
#define EXCEPTION_FLT_OVERFLOW             0xC0000091L
#define EXCEPTION_FLT_STACK_CHECK          0xC0000092L
#define EXCEPTION_FLT_UNDERFLOW            0xC0000093L
#define EXCEPTION_INT_DIVIDE_BY_ZERO       0xC0000094L
#define EXCEPTION_INT_OVERFLOW             0xC0000095L
#define EXCEPTION_PRIV_INSTRUCTION         0xC0000096L
#define EXCEPTION_IN_PAGE_ERROR            0xC0000006L
#define EXCEPTION_ILLEGAL_INSTRUCTION      0xC000001DL
#define EXCEPTION_NONCONTINUABLE_EXCEPTION 0xC0000025L
#define EXCEPTION_STACK_OVERFLOW           0xC00000FDL
#define EXCEPTION_INVALID_DISPOSITION      0xC0000026L
#define EXCEPTION_GUARD_PAGE               0x80000001L
#define EXCEPTION_INVALID_HANDLE           0xC0000008L

/* ---------------------------------------------------------------------------
** Constants
** ------------------------------------------------------------------------- */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE    ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INFINITE             0xFFFFFFFF
#define WAIT_OBJECT_0        0x00000000L
#define WAIT_ABANDONED       0x00000080L
#define WAIT_TIMEOUT         0x00000102L
#define WAIT_FAILED          ((DWORD)0xFFFFFFFF)
#define STILL_ACTIVE         0x00000103L

#define _MAX_PATH   260
#define _MAX_DRIVE  3
#define _MAX_DIR    256
#define _MAX_FNAME  256
#define _MAX_EXT    256
#define MAX_COMPUTERNAME_LENGTH 15
#define UNLEN 256

#define ERROR_SUCCESS             0L
#define NO_ERROR                  0L
#define ERROR_FILE_NOT_FOUND      2L
#define ERROR_PATH_NOT_FOUND      3L
#define ERROR_ACCESS_DENIED       5L
#define ERROR_INVALID_HANDLE      6L
#define ERROR_NOT_ENOUGH_MEMORY   8L
#define ERROR_NO_MORE_FILES       18L
#define ERROR_HANDLE_EOF          38L
#define ERROR_FILE_EXISTS         80L
#define ERROR_INVALID_PARAMETER   87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_ALREADY_EXISTS      183L
#define ERROR_MORE_DATA           234L
#define ERROR_NO_MORE_ITEMS       259L
#define ERROR_IO_PENDING          997L

/* HRESULT */
#define S_OK                     ((HRESULT)0L)
#define S_FALSE                  ((HRESULT)1L)
#define E_UNEXPECTED             ((HRESULT)0x8000FFFFL)
#define E_NOTIMPL                ((HRESULT)0x80004001L)
#define E_OUTOFMEMORY            ((HRESULT)0x8007000EL)
#define E_INVALIDARG             ((HRESULT)0x80070057L)
#define E_NOINTERFACE            ((HRESULT)0x80004002L)
#define E_POINTER                ((HRESULT)0x80004003L)
#define E_HANDLE                 ((HRESULT)0x80070006L)
#define E_ABORT                  ((HRESULT)0x80004004L)
#define E_FAIL                   ((HRESULT)0x80004005L)
#define E_ACCESSDENIED           ((HRESULT)0x80070005L)
#define CLASS_E_NOAGGREGATION    ((HRESULT)0x80040110L)
#define DISP_E_MEMBERNOTFOUND    ((HRESULT)0x80020003L)
#define DISP_E_UNKNOWNNAME       ((HRESULT)0x80020006L)
#define SUCCEEDED(hr)            (((HRESULT)(hr)) >= 0)
#define FAILED(hr)               (((HRESULT)(hr)) < 0)
#define HRESULT_CODE(hr)         ((hr) & 0xFFFF)
#define HRESULT_FACILITY(hr)     (((hr) >> 16) & 0x1fff)
#define SEVERITY_SUCCESS         0
#define SEVERITY_ERROR           1
#define FACILITY_WIN32           7
#define FACILITY_ITF             4
#define MAKE_HRESULT(sev,fac,code) \
	((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define HRESULT_FROM_WIN32(x) \
	((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000)))

/* Files */
#define GENERIC_READ             0x80000000L
#define GENERIC_WRITE            0x40000000L
#define GENERIC_EXECUTE          0x20000000L
#define GENERIC_ALL              0x10000000L
#define FILE_SHARE_READ          0x00000001
#define FILE_SHARE_WRITE         0x00000002
#define FILE_SHARE_DELETE        0x00000004
#define CREATE_NEW               1
#define CREATE_ALWAYS            2
#define OPEN_EXISTING            3
#define OPEN_ALWAYS              4
#define TRUNCATE_EXISTING        5
#define FILE_BEGIN               0
#define FILE_CURRENT             1
#define FILE_END                 2
#define FILE_ATTRIBUTE_READONLY  0x00000001
#define FILE_ATTRIBUTE_HIDDEN    0x00000002
#define FILE_ATTRIBUTE_SYSTEM    0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE   0x00000020
#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_FLAG_WRITE_THROUGH  0x80000000
#define FILE_FLAG_OVERLAPPED     0x40000000
#define FILE_FLAG_NO_BUFFERING   0x20000000
#define FILE_FLAG_RANDOM_ACCESS  0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_MAP_READ            0x0004
#define FILE_MAP_WRITE           0x0002
#define FILE_MAP_ALL_ACCESS      0x000F001F
#define PAGE_NOACCESS            0x01
#define PAGE_READONLY            0x02
#define PAGE_READWRITE           0x04
#define PAGE_EXECUTE_READWRITE   0x40
#define MEM_COMMIT               0x1000
#define MEM_RESERVE              0x2000
#define MEM_DECOMMIT             0x4000
#define MEM_RELEASE              0x8000
#define DRIVE_UNKNOWN            0
#define DRIVE_NO_ROOT_DIR        1
#define DRIVE_REMOVABLE          2
#define DRIVE_FIXED              3
#define DRIVE_REMOTE             4
#define DRIVE_CDROM              5
#define DRIVE_RAMDISK            6

/* Global memory */
#define GMEM_FIXED       0x0000
#define GMEM_MOVEABLE    0x0002
#define GMEM_ZEROINIT    0x0040
#define GHND             (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR             (GMEM_FIXED | GMEM_ZEROINIT)
#define LMEM_FIXED       0x0000
#define LMEM_ZEROINIT    0x0040
#define LPTR             (LMEM_FIXED | LMEM_ZEROINIT)
#define HEAP_ZERO_MEMORY 0x00000008

/* Threads and processes */
#define THREAD_PRIORITY_LOWEST        (-2)
#define THREAD_PRIORITY_BELOW_NORMAL  (-1)
#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_IDLE          (-15)
#define NORMAL_PRIORITY_CLASS         0x00000020
#define IDLE_PRIORITY_CLASS           0x00000040
#define HIGH_PRIORITY_CLASS           0x00000080
#define REALTIME_PRIORITY_CLASS       0x00000100
#define CREATE_SUSPENDED              0x00000004
#define CREATE_NEW_CONSOLE            0x00000010
#define DETACHED_PROCESS              0x00000008
#define SYNCHRONIZE                   0x00100000L
#define EVENT_ALL_ACCESS              0x001F0003
#define MUTEX_ALL_ACCESS              0x001F0001
#define PROCESS_ALL_ACCESS            0x001F0FFF
#define STARTF_USESHOWWINDOW          0x00000001

/* Locale */
#define CP_ACP                0
#define CP_OEMCP              1
#define CP_UTF8               65001
#define MB_PRECOMPOSED        0x00000001
#define WC_COMPOSITECHECK     0x00000200
#define LOCALE_SYSTEM_DEFAULT 0x0800
#define LOCALE_USER_DEFAULT   0x0400
#define DATE_SHORTDATE        0x00000001
#define DATE_LONGDATE         0x00000002
#define TIME_NOSECONDS        0x00000002
#define LANG_NEUTRAL          0x00
#define LANG_ENGLISH          0x09
#define SUBLANG_DEFAULT       0x01
#define SUBLANG_ENGLISH_US    0x01
#define MAKELANGID(p, s)      ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(lgid)   ((WORD)(lgid) & 0x3ff)
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_STRING     0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE    0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000

/* Registry */
#define HKEY_CLASSES_ROOT     ((HKEY)(ULONG_PTR)0x80000000)
#define HKEY_CURRENT_USER     ((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE    ((HKEY)(ULONG_PTR)0x80000002)
#define HKEY_USERS            ((HKEY)(ULONG_PTR)0x80000003)
#define KEY_QUERY_VALUE       0x0001
#define KEY_SET_VALUE         0x0002
#define KEY_CREATE_SUB_KEY    0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ              0x20019
#define KEY_WRITE             0x20006
#define KEY_EXECUTE           0x20019
#define KEY_ALL_ACCESS        0xF003F
#define REG_NONE              0
#define REG_SZ                1
#define REG_EXPAND_SZ         2
#define REG_BINARY            3
#define REG_DWORD             4
#define REG_MULTI_SZ          7
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_CREATED_NEW_KEY   0x00000001
#define REG_OPENED_EXISTING_KEY 0x00000002

/* Message boxes */
#define MB_OK               0x00000000L
#define MB_OKCANCEL         0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL      0x00000003L
#define MB_YESNO            0x00000004L
#define MB_RETRYCANCEL      0x00000005L
#define MB_ICONHAND         0x00000010L
#define MB_ICONQUESTION     0x00000020L
#define MB_ICONEXCLAMATION  0x00000030L
#define MB_ICONASTERISK     0x00000040L
#define MB_ICONWARNING      MB_ICONEXCLAMATION
#define MB_ICONERROR        MB_ICONHAND
#define MB_ICONSTOP         MB_ICONHAND
#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_DEFBUTTON1       0x00000000L
#define MB_DEFBUTTON2       0x00000100L
#define MB_APPLMODAL        0x00000000L
#define MB_SYSTEMMODAL      0x00001000L
#define MB_TASKMODAL        0x00002000L
#define MB_SETFOREGROUND    0x00010000L
#define MB_TOPMOST          0x00040000L
#define IDOK     1
#define IDCANCEL 2
#define IDABORT  3
#define IDRETRY  4
#define IDIGNORE 5
#define IDYES    6
#define IDNO     7

/* Window messages */
#define WM_NULL           0x0000
#define WM_CREATE         0x0001
#define WM_DESTROY        0x0002
#define WM_MOVE           0x0003
#define WM_SIZE           0x0005
#define WM_ACTIVATE       0x0006
#define WM_SETFOCUS       0x0007
#define WM_KILLFOCUS      0x0008
#define WM_ENABLE         0x000A
#define WM_SETREDRAW      0x000B
#define WM_SETTEXT        0x000C
#define WM_GETTEXT        0x000D
#define WM_PAINT          0x000F
#define WM_CLOSE          0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT           0x0012
#define WM_ERASEBKGND     0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_ENDSESSION     0x0016
#define WM_SHOWWINDOW     0x0018
#define WM_ACTIVATEAPP    0x001C
#define WM_SETCURSOR      0x0020
#define WM_MOUSEACTIVATE  0x0021
#define WM_GETMINMAXINFO  0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE   0x0219
#define WM_NCCREATE       0x0081
#define WM_NCDESTROY      0x0082
#define WM_NCHITTEST      0x0084
#define WM_NCPAINT        0x0085
#define WM_NCACTIVATE     0x0086
#define WM_NCMOUSEMOVE    0x00A0
#define WM_NCLBUTTONDOWN  0x00A1
#define WM_KEYFIRST       0x0100
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_DEADCHAR       0x0103
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#define WM_SYSCHAR        0x0106
#define WM_KEYLAST        0x0108
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_IME_KEYLAST    0x010F
#define WM_INITDIALOG     0x0110
#define WM_COMMAND        0x0111
#define WM_SYSCOMMAND     0x0112
#define WM_TIMER          0x0113
#define WM_HSCROLL        0x0114
#define WM_VSCROLL        0x0115
#define WM_MENUSELECT     0x011F
#define WM_ENTERIDLE      0x0121
#define WM_MOUSEFIRST     0x0200
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_LBUTTONDBLCLK  0x0203
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_RBUTTONDBLCLK  0x0206
#define WM_MBUTTONDOWN    0x0207
#define WM_MBUTTONUP      0x0208
#define WM_MBUTTONDBLCLK  0x0209
#define WM_MOUSEWHEEL     0x020A
#define WM_MOUSELAST      0x020A
#define WM_SIZING         0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_MOVING         0x0216
#define WM_ENTERSIZEMOVE  0x0231
#define WM_EXITSIZEMOVE   0x0232
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY     0x0282
#define WM_IME_CONTROL    0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT     0x0285
#define WM_IME_CHAR       0x0286
#define WM_IME_REQUEST    0x0288
#define WM_IME_KEYDOWN    0x0290
#define WM_IME_KEYUP      0x0291
#define WM_USER           0x0400
#define WM_APP            0x8000
#define WHEEL_DELTA       120
#define MK_LBUTTON        0x0001
#define MK_RBUTTON        0x0002
#define MK_SHIFT          0x0004
#define MK_CONTROL        0x0008
#define MK_MBUTTON        0x0010
#define WA_INACTIVE       0
#define WA_ACTIVE         1
#define WA_CLICKACTIVE    2
#define SC_SCREENSAVE     0xF140
#define SC_MONITORPOWER   0xF170
#define SC_KEYMENU        0xF100
#define SC_CLOSE          0xF060
#define SC_MINIMIZE       0xF020
#define SC_MAXIMIZE       0xF030
#define SIZE_RESTORED     0
#define SIZE_MINIMIZED    1
#define SIZE_MAXIMIZED    2
#define PM_NOREMOVE       0x0000
#define PM_REMOVE         0x0001
#define PM_NOYIELD        0x0002
#define HTCLIENT          1
#define PBT_APMQUERYSUSPEND 0x0000
#define PBT_APMRESUMESUSPEND 0x0007
#define BROADCAST_QUERY_DENY 0x424D5144

/* Windows */
#define CS_VREDRAW          0x0001
#define CS_HREDRAW          0x0002
#define CS_DBLCLKS          0x0008
#define CS_OWNDC            0x0020
#define WS_OVERLAPPED       0x00000000L
#define WS_POPUP            0x80000000L
#define WS_CHILD            0x40000000L
#define WS_MINIMIZE         0x20000000L
#define WS_VISIBLE          0x10000000L
#define WS_DISABLED         0x08000000L
#define WS_CLIPSIBLINGS     0x04000000L
#define WS_CLIPCHILDREN     0x02000000L
#define WS_MAXIMIZE         0x01000000L
#define WS_CAPTION          0x00C00000L
#define WS_BORDER           0x00800000L
#define WS_DLGFRAME         0x00400000L
#define WS_VSCROLL          0x00200000L
#define WS_HSCROLL          0x00100000L
#define WS_SYSMENU          0x00080000L
#define WS_THICKFRAME       0x00040000L
#define WS_MINIMIZEBOX      0x00020000L
#define WS_MAXIMIZEBOX      0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW      (WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST       0x00000008L
#define WS_EX_APPWINDOW     0x00040000L
#define SW_HIDE             0
#define SW_SHOWNORMAL       1
#define SW_NORMAL           1
#define SW_SHOWMINIMIZED    2
#define SW_SHOWMAXIMIZED    3
#define SW_MAXIMIZE         3
#define SW_SHOWNOACTIVATE   4
#define SW_SHOW             5
#define SW_MINIMIZE         6
#define SW_SHOWMINNOACTIVE  7
#define SW_SHOWNA           8
#define SW_RESTORE          9
#define SW_SHOWDEFAULT      10
#define SWP_NOSIZE          0x0001
#define SWP_NOMOVE          0x0002
#define SWP_NOZORDER        0x0004
#define SWP_NOREDRAW        0x0008
#define SWP_NOACTIVATE      0x0010
#define SWP_SHOWWINDOW      0x0040
#define SWP_HIDEWINDOW      0x0080
#define HWND_TOP            ((HWND)0)
#define HWND_BOTTOM         ((HWND)1)
#define HWND_TOPMOST        ((HWND)-1)
#define HWND_NOTOPMOST      ((HWND)-2)
#define HWND_DESKTOP        ((HWND)0)
#define GWL_WNDPROC         (-4)
#define GWL_HINSTANCE       (-6)
#define GWL_STYLE           (-16)
#define GWL_EXSTYLE         (-20)
#define GWL_USERDATA        (-21)
#define GWL_ID              (-12)
#define CW_USEDEFAULT       ((int)0x80000000)
#define SM_CXSCREEN         0
#define SM_CYSCREEN         1
#define SM_CXCURSOR         13
#define SM_CYCURSOR         14
#define SM_CXFRAME          32
#define SM_CYFRAME          33
#define SM_CYCAPTION        4
#define SM_CXDOUBLECLK      36
#define SM_CYDOUBLECLK      37
#define SM_MOUSEWHEELPRESENT 75
#define IDC_ARROW           ((LPCSTR)32512)
#define IDC_WAIT            ((LPCSTR)32514)
#define IDI_APPLICATION     ((LPCSTR)32512)
#define IMAGE_BITMAP        0
#define IMAGE_ICON          1
#define IMAGE_CURSOR        2
#define LR_DEFAULTCOLOR     0x0000
#define LR_LOADFROMFILE     0x0010
#define LR_CREATEDIBSECTION 0x2000
#define LR_DEFAULTSIZE      0x0040
#define COLOR_WINDOW        5
#define BLACK_BRUSH         4
#define WHITE_BRUSH         0
#define NULL_BRUSH          5
#define DEFAULT_GUI_FONT    17
#define TRANSPARENT         1
#define OPAQUE              2
#define SRCCOPY             0x00CC0020
#define BI_RGB              0L
#define BI_BITFIELDS        3L
#define DIB_RGB_COLORS      0
#define FW_NORMAL           400
#define FW_BOLD             700
#define ANSI_CHARSET        0
#define DEFAULT_CHARSET     1
#define OUT_DEFAULT_PRECIS  0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 4
#define NONANTIALIASED_QUALITY 3
#define DEFAULT_QUALITY     0
#define DEFAULT_PITCH       0
#define VARIABLE_PITCH      2
#define FF_DONTCARE         0
#define ETO_OPAQUE          0x0002
#define ETO_CLIPPED         0x0004
#define DT_LEFT             0x00000000
#define DT_CENTER           0x00000001
#define DT_RIGHT            0x00000002
#define DT_VCENTER          0x00000004
#define DT_SINGLELINE       0x00000020
#define DT_NOCLIP           0x00000100
#define DT_CALCRECT         0x00000400
#define DT_NOPREFIX         0x00000800
#define MONITOR_DEFAULTTONEAREST 2
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define CDS_FULLSCREEN      0x00000004
#define DISP_CHANGE_SUCCESSFUL 0
#define VER_PLATFORM_WIN32s        0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT      2
#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_INTEL_PENTIUM 586
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH  2
#define DLL_THREAD_DETACH  3
#define DLL_PROCESS_DETACH 0
#define SEM_FAILCRITICALERRORS 0x0001

/* Virtual keys */
#define VK_LBUTTON    0x01
#define VK_RBUTTON    0x02
#define VK_CANCEL     0x03
#define VK_MBUTTON    0x04
#define VK_BACK       0x08
#define VK_TAB        0x09
#define VK_CLEAR      0x0C
#define VK_RETURN     0x0D
#define VK_SHIFT      0x10
#define VK_CONTROL    0x11
#define VK_MENU       0x12
#define VK_PAUSE      0x13
#define VK_CAPITAL    0x14
#define VK_ESCAPE     0x1B
#define VK_SPACE      0x20
#define VK_PRIOR      0x21
#define VK_NEXT       0x22
#define VK_END        0x23
#define VK_HOME       0x24
#define VK_LEFT       0x25
#define VK_UP         0x26
#define VK_RIGHT      0x27
#define VK_DOWN       0x28
#define VK_SELECT     0x29
#define VK_PRINT      0x2A
#define VK_EXECUTE    0x2B
#define VK_SNAPSHOT   0x2C
#define VK_INSERT     0x2D
#define VK_DELETE     0x2E
#define VK_HELP       0x2F
#define VK_LWIN       0x5B
#define VK_RWIN       0x5C
#define VK_APPS       0x5D
#define VK_NUMPAD0    0x60
#define VK_NUMPAD1    0x61
#define VK_NUMPAD2    0x62
#define VK_NUMPAD3    0x63
#define VK_NUMPAD4    0x64
#define VK_NUMPAD5    0x65
#define VK_NUMPAD6    0x66
#define VK_NUMPAD7    0x67
#define VK_NUMPAD8    0x68
#define VK_NUMPAD9    0x69
#define VK_MULTIPLY   0x6A
#define VK_ADD        0x6B
#define VK_SEPARATOR  0x6C
#define VK_SUBTRACT   0x6D
#define VK_DECIMAL    0x6E
#define VK_DIVIDE     0x6F
#define VK_F1         0x70
#define VK_F2         0x71
#define VK_F3         0x72
#define VK_F4         0x73
#define VK_F5         0x74
#define VK_F6         0x75
#define VK_F7         0x76
#define VK_F8         0x77
#define VK_F9         0x78
#define VK_F10        0x79
#define VK_F11        0x7A
#define VK_F12        0x7B
#define VK_NUMLOCK    0x90
#define VK_SCROLL     0x91
#define VK_LSHIFT     0xA0
#define VK_RSHIFT     0xA1
#define VK_LCONTROL   0xA2
#define VK_RCONTROL   0xA3
#define VK_LMENU      0xA4
#define VK_RMENU      0xA5
#define VK_OEM_1      0xBA
#define VK_OEM_PLUS   0xBB
#define VK_OEM_COMMA  0xBC
#define VK_OEM_MINUS  0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2      0xBF
#define VK_OEM_3      0xC0
#define VK_OEM_4      0xDB
#define VK_OEM_5      0xDC
#define VK_OEM_6      0xDD
#define VK_OEM_7      0xDE

/* ---------------------------------------------------------------------------
** Macros
** ------------------------------------------------------------------------- */
#define MAKEWORD(a, b)   ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b)   ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define LOWORD(l)        ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)        ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w)        ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w)        ((BYTE)((DWORD_PTR)(w) >> 8))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))
#define RGB(r, g, b)     ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb)   (LOBYTE(rgb))
#define GetGValue(rgb)   (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)   (LOBYTE((rgb) >> 16))
#define MAKEINTRESOURCE(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEA MAKEINTRESOURCE
#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)
#define RT_BITMAP        MAKEINTRESOURCE(2)
#define RT_ICON          MAKEINTRESOURCE(3)
#define RT_RCDATA        MAKEINTRESOURCE(10)
#define UNREFERENCED_PARAMETER(P) (void)(P)
#define ZeroMemory(d, l)     memset((d), 0, (l))
#define FillMemory(d, l, f)  memset((d), (f), (l))
#define CopyMemory(d, s, l)  memcpy((d), (s), (l))
#define MoveMemory(d, s, l)  memmove((d), (s), (l))
#define RtlZeroMemory ZeroMemory
#define RtlCopyMemory CopyMemory
#define RtlMoveMemory MoveMemory
#define RtlFillMemory FillMemory
#define TEXT(s) s
#define __TEXT(s) s
#define _T(s) s
#define _TEXT(s) s

/* ---------------------------------------------------------------------------
** Functions (compat/src/win32.cpp)
** ------------------------------------------------------------------------- */

/* Time */
DWORD   WINAPI GetTickCount(void);
BOOL    WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
BOOL    WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
void    WINAPI Sleep(DWORD dwMilliseconds);
DWORD   WINAPI SleepEx(DWORD dwMilliseconds, BOOL bAlertable);
void    WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime);
void    WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime);
void    WINAPI GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
BOOL    WINAPI SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime);
BOOL    WINAPI FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime);
BOOL    WINAPI FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime);
BOOL    WINAPI LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, LPFILETIME lpFileTime);
BOOL    WINAPI FileTimeToDosDateTime(const FILETIME *lpFileTime, LPWORD lpFatDate, LPWORD lpFatTime);
BOOL    WINAPI DosDateTimeToFileTime(WORD wFatDate, WORD wFatTime, LPFILETIME lpFileTime);
LONG    WINAPI CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2);
DWORD   WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation);
int     WINAPI GetDateFormatA(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpDate, LPCSTR lpFormat, LPSTR lpDateStr, int cchDate);
int     WINAPI GetTimeFormatA(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpTime, LPCSTR lpFormat, LPSTR lpTimeStr, int cchTime);
int     WINAPI GetDateFormatW(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpDate, LPCWSTR lpFormat, LPWSTR lpDateStr, int cchDate);
int     WINAPI GetTimeFormatW(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpTime, LPCWSTR lpFormat, LPWSTR lpTimeStr, int cchTime);
#define GetDateFormat GetDateFormatA
#define GetTimeFormat GetTimeFormatA

/* Errors and debugging */
DWORD   WINAPI GetLastError(void);
void    WINAPI SetLastError(DWORD dwErrCode);
void    WINAPI OutputDebugStringA(LPCSTR lpOutputString);
void    WINAPI OutputDebugStringW(LPCWSTR lpOutputString);
#define OutputDebugString OutputDebugStringA
void    WINAPI DebugBreak(void);
BOOL    WINAPI IsDebuggerPresent(void);
BOOL    WINAPI IsBadReadPtr(const void *lp, UINT_PTR ucb);
BOOL    WINAPI IsBadWritePtr(LPVOID lp, UINT_PTR ucb);
BOOL    WINAPI IsBadCodePtr(FARPROC lpfn);
BOOL    WINAPI IsBadStringPtrA(LPCSTR lpsz, UINT_PTR ucchMax);
UINT    WINAPI SetErrorMode(UINT uMode);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
DWORD   WINAPI FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, va_list *Arguments);
DWORD   WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize, va_list *Arguments);
#define FormatMessage FormatMessageA
int     WINAPI MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
int     WINAPI MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
#define MessageBox MessageBoxA
BOOL    WINAPI MessageBeep(UINT uType);
void    WINAPI FatalAppExitA(UINT uAction, LPCSTR lpMessageText);
#define FatalAppExit FatalAppExitA

/* Synchronisation */
void    WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
BOOL    WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount);
void    WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void    WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
BOOL    WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void    WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
LONG    WINAPI InterlockedIncrement(LONG volatile *lpAddend);
LONG    WINAPI InterlockedDecrement(LONG volatile *lpAddend);
LONG    WINAPI InterlockedExchange(LONG volatile *Target, LONG Value);
LONG    WINAPI InterlockedExchangeAdd(LONG volatile *Addend, LONG Value);
LONG    WINAPI InterlockedCompareExchange(LONG volatile *Destination, LONG Exchange, LONG Comperand);
HANDLE  WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);
HANDLE  WINAPI OpenMutexA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);
BOOL    WINAPI ReleaseMutex(HANDLE hMutex);
#define CreateMutex CreateMutexA
#define OpenMutex OpenMutexA
HANDLE  WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);
HANDLE  WINAPI OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);
BOOL    WINAPI SetEvent(HANDLE hEvent);
BOOL    WINAPI ResetEvent(HANDLE hEvent);
BOOL    WINAPI PulseEvent(HANDLE hEvent);
#define CreateEvent CreateEventA
#define OpenEvent OpenEventA
HANDLE  WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCSTR lpName);
BOOL    WINAPI ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount);
#define CreateSemaphore CreateSemaphoreA
DWORD   WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD   WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);
BOOL    WINAPI CloseHandle(HANDLE hObject);
BOOL    WINAPI DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle, LPHANDLE lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions);

/* Threads and processes */
HANDLE  WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
HANDLE  WINAPI GetCurrentThread(void);
DWORD   WINAPI GetCurrentThreadId(void);
HANDLE  WINAPI GetCurrentProcess(void);
DWORD   WINAPI GetCurrentProcessId(void);
BOOL    WINAPI SetThreadPriority(HANDLE hThread, int nPriority);
int     WINAPI GetThreadPriority(HANDLE hThread);
BOOL    WINAPI SetPriorityClass(HANDLE hProcess, DWORD dwPriorityClass);
DWORD   WINAPI GetPriorityClass(HANDLE hProcess);
BOOL    WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode);
BOOL    WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);
void    WINAPI ExitThread(DWORD dwExitCode);
DWORD   WINAPI SuspendThread(HANDLE hThread);
DWORD   WINAPI ResumeThread(HANDLE hThread);
BOOL    WINAPI GetThreadContext(HANDLE hThread, LPCONTEXT lpContext);
BOOL    WINAPI SwitchToThread(void);
void    WINAPI ExitProcess(UINT uExitCode);
BOOL    WINAPI TerminateProcess(HANDLE hProcess, UINT uExitCode);
BOOL    WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
BOOL    WINAPI CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFO lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
#define CreateProcess CreateProcessA
LPSTR   WINAPI GetCommandLineA(void);
#define GetCommandLine GetCommandLineA
DWORD   WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);
BOOL    WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
#define GetEnvironmentVariable GetEnvironmentVariableA
#define SetEnvironmentVariable SetEnvironmentVariableA
DWORD   WINAPI TlsAlloc(void);
BOOL    WINAPI TlsFree(DWORD dwTlsIndex);
LPVOID  WINAPI TlsGetValue(DWORD dwTlsIndex);
BOOL    WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue);
#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)

/* Modules */
HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName);
HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);
DWORD   WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
BOOL    WINAPI FreeLibrary(HMODULE hLibModule);
#define LoadLibrary LoadLibraryA
#define GetModuleHandle GetModuleHandleA
#define GetModuleFileName GetModuleFileNameA
HRSRC   WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo);
LPVOID  WINAPI LockResource(HGLOBAL hResData);
DWORD   WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo);
#define FindResource FindResourceA
int     WINAPI LoadStringA(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax);
#define LoadString LoadStringA

/* System information */
BOOL    WINAPI GetVersionExA(LPOSVERSIONINFO lpVersionInformation);
#define GetVersionEx GetVersionExA
DWORD   WINAPI GetVersion(void);
void    WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);
void    WINAPI GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer);
BOOL    WINAPI GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize);
BOOL    WINAPI GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer);
#define GetComputerName GetComputerNameA
#define GetUserName GetUserNameA
LANGID  WINAPI GetSystemDefaultLangID(void);
LANGID  WINAPI GetUserDefaultLangID(void);
LCID    WINAPI GetUserDefaultLCID(void);
LCID    WINAPI GetSystemDefaultLCID(void);
UINT    WINAPI GetACP(void);
HKL     WINAPI GetKeyboardLayout(DWORD idThread);
int     WINAPI GetSystemMetrics(int nIndex);
UINT    WINAPI GetDoubleClickTime(void);
DWORD   WINAPI GetLogicalDrives(void);
DWORD   WINAPI GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer);
UINT    WINAPI GetDriveTypeA(LPCSTR lpRootPathName);
BOOL    WINAPI GetVolumeInformationA(LPCSTR lpRootPathName, LPSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);
BOOL    WINAPI GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters);
BOOL    WINAPI GetDiskFreeSpaceExA(LPCSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailableToCaller, PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes);
#define GetLogicalDriveStrings GetLogicalDriveStringsA
#define GetDriveType GetDriveTypeA
#define GetVolumeInformation GetVolumeInformationA
#define GetDiskFreeSpace GetDiskFreeSpaceA
#define GetDiskFreeSpaceEx GetDiskFreeSpaceExA
UINT    WINAPI GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize);
UINT    WINAPI GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize);
DWORD   WINAPI GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
UINT    WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName);
#define GetWindowsDirectory GetWindowsDirectoryA
#define GetSystemDirectory GetSystemDirectoryA
#define GetTempPath GetTempPathA
#define GetTempFileName GetTempFileNameA

/* Memory */
HGLOBAL WINAPI GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags);
HGLOBAL WINAPI GlobalFree(HGLOBAL hMem);
LPVOID  WINAPI GlobalLock(HGLOBAL hMem);
BOOL    WINAPI GlobalUnlock(HGLOBAL hMem);
SIZE_T  WINAPI GlobalSize(HGLOBAL hMem);
HLOCAL  WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes);
HLOCAL  WINAPI LocalFree(HLOCAL hMem);
LPVOID  WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
BOOL    WINAPI VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);
BOOL    WINAPI VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect);
HANDLE  WINAPI GetProcessHeap(void);
HANDLE  WINAPI HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize);
BOOL    WINAPI HeapDestroy(HANDLE hHeap);
LPVOID  WINAPI HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
LPVOID  WINAPI HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);
BOOL    WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
SIZE_T  WINAPI HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem);

/* Files */
HANDLE  WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
#define CreateFile CreateFileA
BOOL    WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
BOOL    WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
DWORD   WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
BOOL    WINAPI SetEndOfFile(HANDLE hFile);
DWORD   WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
BOOL    WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime);
BOOL    WINAPI SetFileTime(HANDLE hFile, const FILETIME *lpCreationTime, const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime);
BOOL    WINAPI GetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation);
BOOL    WINAPI FlushFileBuffers(HANDLE hFile);
DWORD   WINAPI GetFileType(HANDLE hFile);
#define FILE_TYPE_DISK 0x0001
BOOL    WINAPI DeleteFileA(LPCSTR lpFileName);
BOOL    WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists);
BOOL    WINAPI MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName);
BOOL    WINAPI MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags);
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
BOOL    WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL    WINAPI RemoveDirectoryA(LPCSTR lpPathName);
DWORD   WINAPI GetFileAttributesA(LPCSTR lpFileName);
BOOL    WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes);
DWORD   WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
BOOL    WINAPI SetCurrentDirectoryA(LPCSTR lpPathName);
DWORD   WINAPI GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart);
DWORD   WINAPI GetShortPathNameA(LPCSTR lpszLongPath, LPSTR lpszShortPath, DWORD cchBuffer);
HANDLE  WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
BOOL    WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
BOOL    WINAPI FindClose(HANDLE hFindFile);
#define DeleteFile DeleteFileA
#define CopyFile CopyFileA
#define MoveFile MoveFileA
#define MoveFileEx MoveFileExA
#define CreateDirectory CreateDirectoryA
#define RemoveDirectory RemoveDirectoryA
#define GetFileAttributes GetFileAttributesA
#define SetFileAttributes SetFileAttributesA
#define GetCurrentDirectory GetCurrentDirectoryA
#define SetCurrentDirectory SetCurrentDirectoryA
#define GetFullPathName GetFullPathNameA
#define GetShortPathName GetShortPathNameA
#define FindFirstFile FindFirstFileA
#define FindNextFile FindNextFileA
HANDLE  WINAPI CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName);
HANDLE  WINAPI OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);
LPVOID  WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap);
LPVOID  WINAPI MapViewOfFileEx(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap, LPVOID lpBaseAddress);
BOOL    WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress);
#define CreateFileMapping CreateFileMappingA
#define OpenFileMapping OpenFileMappingA
HFILE   WINAPI _lopen(LPCSTR lpPathName, int iReadWrite);
HFILE   WINAPI _lclose(HFILE hFile);

/* Profile (.ini) files */
UINT    WINAPI GetPrivateProfileIntA(LPCSTR lpAppName, LPCSTR lpKeyName, INT nDefault, LPCSTR lpFileName);
DWORD   WINAPI GetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString, DWORD nSize, LPCSTR lpFileName);
BOOL    WINAPI WritePrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpString, LPCSTR lpFileName);
#define GetPrivateProfileInt GetPrivateProfileIntA
#define GetPrivateProfileString GetPrivateProfileStringA
#define WritePrivateProfileString WritePrivateProfileStringA

/* Registry: an in-memory registry seeded with the keys the game expects
** (see Dependencies/WebCompat/src/registry.cpp). */
LONG    WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
LONG    WINAPI RegOpenKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult);
LONG    WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
LONG    WINAPI RegCreateKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult);
LONG    WINAPI RegCloseKey(HKEY hKey);
LONG    WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG    WINAPI RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG    WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
LONG    WINAPI RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
LONG    WINAPI RegDeleteValueA(HKEY hKey, LPCSTR lpValueName);
LONG    WINAPI RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey);
LONG    WINAPI RegEnumValueA(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG    WINAPI RegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName, LPDWORD lpReserved, LPSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime);
LONG    WINAPI RegEnumKeyA(HKEY hKey, DWORD dwIndex, LPSTR lpName, DWORD cchName);
LONG    WINAPI RegQueryInfoKeyA(HKEY hKey, LPSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime);
LONG    WINAPI RegFlushKey(HKEY hKey);
#define RegOpenKeyEx RegOpenKeyExA
#define RegOpenKey RegOpenKeyA
#define RegCreateKeyEx RegCreateKeyExA
#define RegCreateKey RegCreateKeyA
#define RegQueryValueEx RegQueryValueExA
#define RegSetValueEx RegSetValueExA
#define RegDeleteValue RegDeleteValueA
#define RegDeleteKey RegDeleteKeyA
#define RegEnumValue RegEnumValueA
#define RegEnumKeyEx RegEnumKeyExA
#define RegEnumKey RegEnumKeyA
#define RegQueryInfoKey RegQueryInfoKeyA

/* Strings */
int     WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
int     WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
int     WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
int     WINAPIV wsprintfW(LPWSTR, LPCWSTR, ...);
int     WINAPI wvsprintfA(LPSTR, LPCSTR, va_list arglist);
#define wsprintf wsprintfA
#define wvsprintf wvsprintfA
LPSTR   WINAPI lstrcpyA(LPSTR lpString1, LPCSTR lpString2);
LPSTR   WINAPI lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength);
LPSTR   WINAPI lstrcatA(LPSTR lpString1, LPCSTR lpString2);
int     WINAPI lstrlenA(LPCSTR lpString);
int     WINAPI lstrcmpA(LPCSTR lpString1, LPCSTR lpString2);
int     WINAPI lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2);
int     WINAPI lstrlenW(LPCWSTR lpString);
LPWSTR  WINAPI lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2);
LPWSTR  WINAPI lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength);
LPWSTR  WINAPI lstrcatW(LPWSTR lpString1, LPCWSTR lpString2);
int     WINAPI lstrcmpiW(LPCWSTR lpString1, LPCWSTR lpString2);
#define lstrcpy lstrcpyA
#define lstrcpyn lstrcpynA
#define lstrcat lstrcatA
#define lstrlen lstrlenA
#define lstrcmp lstrcmpA
#define lstrcmpi lstrcmpiA
LPSTR   WINAPI CharUpperA(LPSTR lpsz);
LPSTR   WINAPI CharLowerA(LPSTR lpsz);
LPSTR   WINAPI CharNextA(LPCSTR lpsz);
LPSTR   WINAPI CharPrevA(LPCSTR lpszStart, LPCSTR lpszCurrent);
BOOL    WINAPI IsCharAlphaA(CHAR ch);
BOOL    WINAPI IsCharAlphaNumericA(CHAR ch);
BOOL    WINAPI IsDBCSLeadByte(BYTE TestChar);
#define CharUpper CharUpperA
#define CharLower CharLowerA
#define CharNext CharNextA
#define CharPrev CharPrevA
#define IsCharAlpha IsCharAlphaA
#define IsCharAlphaNumeric IsCharAlphaNumericA
int     WINAPI CompareStringA(LCID Locale, DWORD dwCmpFlags, LPCSTR lpString1, int cchCount1, LPCSTR lpString2, int cchCount2);
#define CompareString CompareStringA
#define NORM_IGNORECASE 0x00000001
#define CSTR_LESS_THAN 1
#define CSTR_EQUAL 2
#define CSTR_GREATER_THAN 3
BOOL    WINAPI GetStringTypeExW(LCID Locale, DWORD dwInfoType, LPCWSTR lpSrcStr, int cchSrc, LPWORD lpCharType);
#define CT_CTYPE1 0x00000001
#define C1_UPPER  0x0001
#define C1_LOWER  0x0002
#define C1_DIGIT  0x0004
#define C1_SPACE  0x0008
#define C1_PUNCT  0x0010
#define C1_CNTRL  0x0020
#define C1_BLANK  0x0040
#define C1_XDIGIT 0x0080
#define C1_ALPHA  0x0100
int     WINAPI GetLocaleInfoA(LCID Locale, DWORD LCType, LPSTR lpLCData, int cchData);
#define GetLocaleInfo GetLocaleInfoA
#define LOCALE_SENGLANGUAGE 0x00001001
#define LOCALE_SABBREVLANGNAME 0x00000003
#define LOCALE_IDEFAULTANSICODEPAGE 0x00001004

/* Windowing. The game has a single window: the canvas. These calls are
** routed to the browser platform layer, or do nothing. */
HWND    WINAPI CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
#define CreateWindowEx CreateWindowExA
#define CreateWindowA(c, w, s, x, y, cx, cy, p, m, i, l) CreateWindowExA(0, c, w, s, x, y, cx, cy, p, m, i, l)
#define CreateWindow CreateWindowA
BOOL    WINAPI DestroyWindow(HWND hWnd);
ATOM    WINAPI RegisterClassA(const WNDCLASSA *lpWndClass);
BOOL    WINAPI UnregisterClassA(LPCSTR lpClassName, HINSTANCE hInstance);
#define RegisterClass RegisterClassA
#define UnregisterClass UnregisterClassA
LRESULT WINAPI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define DefWindowProc DefWindowProcA
LRESULT WINAPI CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define CallWindowProc CallWindowProcA
BOOL    WINAPI ShowWindow(HWND hWnd, int nCmdShow);
BOOL    WINAPI UpdateWindow(HWND hWnd);
BOOL    WINAPI IsWindow(HWND hWnd);
BOOL    WINAPI IsWindowVisible(HWND hWnd);
BOOL    WINAPI IsIconic(HWND hWnd);
BOOL    WINAPI IsZoomed(HWND hWnd);
HWND    WINAPI SetFocus(HWND hWnd);
HWND    WINAPI GetFocus(void);
HWND    WINAPI GetActiveWindow(void);
HWND    WINAPI SetActiveWindow(HWND hWnd);
HWND    WINAPI GetForegroundWindow(void);
BOOL    WINAPI SetForegroundWindow(HWND hWnd);
HWND    WINAPI GetDesktopWindow(void);
HWND    WINAPI FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName);
#define FindWindow FindWindowA
HWND    WINAPI GetParent(HWND hWnd);
HWND    WINAPI SetCapture(HWND hWnd);
BOOL    WINAPI ReleaseCapture(void);
HWND    WINAPI GetCapture(void);
BOOL    WINAPI SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
BOOL    WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
BOOL    WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect);
BOOL    WINAPI GetClientRect(HWND hWnd, LPRECT lpRect);
BOOL    WINAPI AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu);
BOOL    WINAPI AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
BOOL    WINAPI ClientToScreen(HWND hWnd, LPPOINT lpPoint);
BOOL    WINAPI ScreenToClient(HWND hWnd, LPPOINT lpPoint);
BOOL    WINAPI GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT *lpwndpl);
BOOL    WINAPI SetWindowPlacement(HWND hWnd, const WINDOWPLACEMENT *lpwndpl);
LONG    WINAPI GetWindowLongA(HWND hWnd, int nIndex);
LONG    WINAPI SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
#define GetWindowLong GetWindowLongA
#define SetWindowLong SetWindowLongA
#define GetWindowLongPtr GetWindowLongA
#define SetWindowLongPtr SetWindowLongA
#define GWLP_WNDPROC GWL_WNDPROC
#define GWLP_USERDATA GWL_USERDATA
BOOL    WINAPI SetWindowTextA(HWND hWnd, LPCSTR lpString);
int     WINAPI GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount);
#define SetWindowText SetWindowTextA
#define GetWindowText GetWindowTextA
BOOL    WINAPI InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
BOOL    WINAPI ValidateRect(HWND hWnd, const RECT *lpRect);
HDC     WINAPI BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint);
BOOL    WINAPI EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint);
BOOL    WINAPI EnableWindow(HWND hWnd, BOOL bEnable);
HWND    WINAPI GetDlgItem(HWND hDlg, int nIDDlgItem);
UINT_PTR WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc);
BOOL    WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent);
HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD dwFlags);

BOOL    WINAPI PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL    WINAPI GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL    WINAPI TranslateMessage(const MSG *lpMsg);
LRESULT WINAPI DispatchMessageA(const MSG *lpMsg);
BOOL    WINAPI PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void    WINAPI PostQuitMessage(int nExitCode);
BOOL    WINAPI WaitMessage(void);
UINT    WINAPI RegisterWindowMessageA(LPCSTR lpString);
#define PeekMessage PeekMessageA
#define GetMessage GetMessageA
#define DispatchMessage DispatchMessageA
#define PostMessage PostMessageA
#define SendMessage SendMessageA
#define RegisterWindowMessage RegisterWindowMessageA

/* Input */
SHORT   WINAPI GetAsyncKeyState(int vKey);
SHORT   WINAPI GetKeyState(int nVirtKey);
BOOL    WINAPI GetKeyboardState(PBYTE lpKeyState);
UINT    WINAPI MapVirtualKeyA(UINT uCode, UINT uMapType);
#define MapVirtualKey MapVirtualKeyA
int     WINAPI ToAscii(UINT uVirtKey, UINT uScanCode, const BYTE *lpKeyState, LPWORD lpChar, UINT uFlags);
BOOL    WINAPI GetCursorPos(LPPOINT lpPoint);
BOOL    WINAPI SetCursorPos(int X, int Y);
int     WINAPI ShowCursor(BOOL bShow);
HCURSOR WINAPI SetCursor(HCURSOR hCursor);
HCURSOR WINAPI GetCursor(void);
HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
HCURSOR WINAPI LoadCursorFromFileA(LPCSTR lpFileName);
BOOL    WINAPI DestroyCursor(HCURSOR hCursor);
#define LoadCursor LoadCursorA
#define LoadCursorFromFile LoadCursorFromFileA
BOOL    WINAPI ClipCursor(const RECT *lpRect);
HICON   WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
#define LoadIcon LoadIconA
HANDLE  WINAPI LoadImageA(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad);
#define LoadImage LoadImageA

/* GDI. Only used by tools and debug output; no-ops in the browser. */
HDC     WINAPI GetDC(HWND hWnd);
int     WINAPI ReleaseDC(HWND hWnd, HDC hDC);
HDC     WINAPI CreateCompatibleDC(HDC hdc);
BOOL    WINAPI DeleteDC(HDC hdc);
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h);
BOOL    WINAPI DeleteObject(HGDIOBJ ho);
HGDIOBJ WINAPI GetStockObject(int i);
int     WINAPI GetObjectA(HANDLE h, int c, LPVOID pv);
#define GetObject GetObjectA
HFONT   WINAPI CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);
#define CreateFont CreateFontA
HFONT   WINAPI CreateFontIndirectA(const LOGFONTA *lplf);
#define CreateFontIndirect CreateFontIndirectA
HBITMAP WINAPI CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, void **ppvBits, HANDLE hSection, DWORD offset);
HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int cx, int cy);
HBRUSH  WINAPI CreateSolidBrush(COLORREF color);
int     WINAPI FillRect(HDC hDC, const RECT *lprc, HBRUSH hbr);
COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color);
COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color);
int     WINAPI SetBkMode(HDC hdc, int mode);
BOOL    WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c);
BOOL    WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c);
#define TextOut TextOutA
BOOL    WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCWSTR lpString, UINT c, const INT *lpDx);
BOOL    WINAPI ExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCSTR lpString, UINT c, const INT *lpDx);
#define ExtTextOut ExtTextOutA
int     WINAPI DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format);
#define DrawText DrawTextA
BOOL    WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);
BOOL    WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl);
#define GetTextExtentPoint32 GetTextExtentPoint32A
BOOL    WINAPI GetTextMetricsA(HDC hdc, LPTEXTMETRIC lptm);
#define GetTextMetrics GetTextMetricsA
BOOL    WINAPI BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop);
int     WINAPI GetDeviceCaps(HDC hdc, int index);
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define VREFRESH 116
COLORREF WINAPI GetPixel(HDC hdc, int x, int y);
COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF color);
int     WINAPI AddFontResourceA(LPCSTR);
BOOL    WINAPI RemoveFontResourceA(LPCSTR lpFileName);
#define AddFontResource AddFontResourceA
#define RemoveFontResource RemoveFontResourceA
BOOL    WINAPI GetDeviceGammaRamp(HDC hdc, LPVOID lpRamp);
BOOL    WINAPI SetDeviceGammaRamp(HDC hdc, LPVOID lpRamp);
int     WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator);
BOOL    WINAPI SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom);
BOOL    WINAPI SetRectEmpty(LPRECT lprc);
BOOL    WINAPI IntersectRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2);
BOOL    WINAPI UnionRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2);
BOOL    WINAPI OffsetRect(LPRECT lprc, int dx, int dy);
BOOL    WINAPI PtInRect(const RECT *lprc, POINT pt);
BOOL    WINAPI IsRectEmpty(const RECT *lprc);

/* Shell */
HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);
#define ShellExecute ShellExecuteA

/* Clipboard */
BOOL    WINAPI OpenClipboard(HWND hWndNewOwner);
BOOL    WINAPI CloseClipboard(void);
BOOL    WINAPI EmptyClipboard(void);
HANDLE  WINAPI GetClipboardData(UINT uFormat);
HANDLE  WINAPI SetClipboardData(UINT uFormat, HANDLE hMem);
BOOL    WINAPI IsClipboardFormatAvailable(UINT format);
#define CF_TEXT 1
#define CF_UNICODETEXT 13

/* ---------------------------------------------------------------------------
** Hooks for the platform layer (Dependencies/WebCompat/src)
** ------------------------------------------------------------------------- */

/* Answers MessageBox calls (e.g. with a dialog in the page). The default logs
** the message and picks the answer that lets the program continue. */
typedef int (*WebCompatMessageBoxHandler)(const char *text, const char *caption, unsigned int type);
void webcompat_set_message_box_handler(WebCompatMessageBoxHandler handler);

/* The size of the canvas, reported as the screen and as the size of the main
** window. */
void webcompat_set_screen_size(int width, int height);

/* The state that GetCursorPos, GetAsyncKeyState and GetKeyState report. Key
** and mouse messages are delivered with PostMessage. */
void webcompat_set_cursor_position(int x, int y);
void webcompat_set_key_state(int virtualKey, int down);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* Windows headers usually pulled in by windows.h */
#include "tchar.h"
#include "mmsystem.h"
#include "winerror.h"
#include "objbase.h"

#endif /* WEBCOMPAT_WINDOWS_H */
