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
** WebAssembly port: the types of the debug help library. The library itself
** cannot be loaded (there is no LoadLibrary), so the game's stack walker finds
** no functions and reports no stack.
*/
#pragma once

#include "windows.h"

typedef enum { AddrMode1616, AddrMode1632, AddrModeReal, AddrModeFlat } ADDRESS_MODE;

typedef struct _tagADDRESS
{
	DWORD        Offset;
	WORD         Segment;
	ADDRESS_MODE Mode;
} ADDRESS, *LPADDRESS;

typedef struct _KDHELP
{
	DWORD Thread;
	DWORD ThCallbackStack;
	DWORD NextCallback;
	DWORD FramePointer;
	DWORD KiCallUserMode;
	DWORD KeUserCallbackDispatcher;
	DWORD SystemRangeStart;
} KDHELP, *PKDHELP;

typedef struct _tagSTACKFRAME
{
	ADDRESS AddrPC;
	ADDRESS AddrReturn;
	ADDRESS AddrFrame;
	ADDRESS AddrStack;
	LPVOID  FuncTableEntry;
	DWORD   Params[4];
	BOOL    Far;
	BOOL    Virtual;
	DWORD   Reserved[3];
	KDHELP  KdHelp;
	ADDRESS AddrBStore;
} STACKFRAME, *LPSTACKFRAME;

typedef BOOL (WINAPI *PREAD_PROCESS_MEMORY_ROUTINE)(HANDLE hProcess, DWORD lpBaseAddress, PVOID lpBuffer, DWORD nSize, PDWORD lpNumberOfBytesRead);
typedef PVOID (WINAPI *PFUNCTION_TABLE_ACCESS_ROUTINE)(HANDLE hProcess, DWORD AddrBase);
typedef DWORD (WINAPI *PGET_MODULE_BASE_ROUTINE)(HANDLE hProcess, DWORD Address);
typedef DWORD (WINAPI *PTRANSLATE_ADDRESS_ROUTINE)(HANDLE hProcess, HANDLE hThread, LPADDRESS lpaddr);

typedef struct _IMAGEHLP_SYMBOL
{
	DWORD SizeOfStruct;
	DWORD Address;
	DWORD Size;
	DWORD Flags;
	DWORD MaxNameLength;
	CHAR  Name[1];
} IMAGEHLP_SYMBOL, *PIMAGEHLP_SYMBOL;

typedef struct _IMAGEHLP_LINE
{
	DWORD SizeOfStruct;
	PVOID Key;
	DWORD LineNumber;
	PCHAR FileName;
	DWORD Address;
} IMAGEHLP_LINE, *PIMAGEHLP_LINE;

#define IMAGE_FILE_MACHINE_I386 0x014c

#define SYMOPT_CASE_INSENSITIVE 0x00000001
#define SYMOPT_UNDNAME          0x00000002
#define SYMOPT_DEFERRED_LOADS   0x00000004
#define SYMOPT_NO_CPP           0x00000008
#define SYMOPT_LOAD_LINES       0x00000010
