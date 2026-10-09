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
** WebAssembly port: the debug heap of the Microsoft C runtime. The web build
** has none: the calls do nothing and the assertions are not checked.
*/
#pragma once

#include "windows.h"

#define _CRTDBG_ALLOC_MEM_DF      0x01
#define _CRTDBG_DELAY_FREE_MEM_DF 0x02
#define _CRTDBG_CHECK_ALWAYS_DF   0x04
#define _CRTDBG_CHECK_CRT_DF      0x10
#define _CRTDBG_LEAK_CHECK_DF     0x20
#define _CRTDBG_REPORT_FLAG       (-1)

#define _CRT_WARN   0
#define _CRT_ERROR  1
#define _CRT_ASSERT 2

#define _CRTDBG_MODE_FILE   0x1
#define _CRTDBG_MODE_DEBUG  0x2
#define _CRTDBG_MODE_WNDW   0x4
#define _CRTDBG_REPORT_MODE (-1)

#define _NORMAL_BLOCK 1

#define _CrtSetDbgFlag(flag)                 ((int)0)
#define _CrtCheckMemory()                    (1)
#define _CrtDumpMemoryLeaks()                (0)
#define _CrtSetReportMode(type, mode)        ((int)0)
#define _CrtSetReportFile(type, file)        ((void *)0)
#define _CrtSetBreakAlloc(number)            ((long)0)
#define _CrtIsValidHeapPointer(p)            (1)
#define _CrtDbgReport(type, file, line, module, ...) (0)

#define _malloc_dbg(size, block, file, line)          malloc(size)
#define _calloc_dbg(count, size, block, file, line)   calloc(count, size)
#define _realloc_dbg(p, size, block, file, line)      realloc(p, size)
#define _expand_dbg(p, size, block)                   ((void *)0)
#define _free_dbg(p, block)                           free(p)
#define _msize_dbg(p, block)                          _msize(p)
