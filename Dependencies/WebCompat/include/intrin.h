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
** WebAssembly port: compiler intrinsics. Most of the Microsoft intrinsics
** (_byteswap_*, _rotl, _ReturnAddress, __debugbreak, ...) are provided by clang
** with -fms-extensions. The x86 specific ones that remain are CPU
** identification and the time stamp counter, for which WebAssembly has no
** equivalent.
*/
#pragma once

#include "windows.h"

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No CPU identification: every leaf reads as zero. */
static inline void __cpuid(int cpuInfo[4], int function)
{
	(void)function;
	cpuInfo[0] = cpuInfo[1] = cpuInfo[2] = cpuInfo[3] = 0;
}

static inline void __cpuidex(int cpuInfo[4], int function, int subfunction)
{
	(void)subfunction;
	__cpuid(cpuInfo, function);
}

/* The monotonic clock in nanoseconds stands in for the time stamp counter. */
static inline unsigned long long __rdtsc(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

#ifdef __cplusplus
}
#endif
