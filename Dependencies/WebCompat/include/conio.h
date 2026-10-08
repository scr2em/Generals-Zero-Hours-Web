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
/* WebAssembly port: console I/O of the Microsoft C runtime. There is no console. */
#pragma once

#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

int _kbhit(void);
int _getch(void);
int _getche(void);
int _putch(int c);

#ifdef __cplusplus
}
#endif
