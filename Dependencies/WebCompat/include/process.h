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
/* WebAssembly port: threads and processes of the Microsoft C runtime. */
#pragma once

#include "windows.h"

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _P_WAIT    0
#define _P_NOWAIT  1
#define _P_OVERLAY 2
#define _P_DETACH  4

uintptr_t _beginthread(void (__cdecl *start_address)(void *), unsigned stack_size, void *arglist);
uintptr_t _beginthreadex(void *security, unsigned stack_size, unsigned (__stdcall *start_address)(void *), void *arglist, unsigned initflag, unsigned *thrdaddr);
void _endthread(void);
void _endthreadex(unsigned retval);

/* A browser tab cannot start other programs: these fail with -1. */
intptr_t _spawnl(int mode, const char *path, const char *arg0, ...);
intptr_t _spawnlp(int mode, const char *path, const char *arg0, ...);
intptr_t _spawnv(int mode, const char *path, const char *const *argv);
intptr_t _spawnvp(int mode, const char *path, const char *const *argv);

#ifdef __cplusplus
}
#endif
