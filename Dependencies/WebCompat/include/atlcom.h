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
** WebAssembly port: the ATL COM interface map macros. The map is what ATL
** uses to implement QueryInterface; the classes here implement it by hand (or
** not at all, see atlbase.h), so the macros expand to nothing.
*/
#pragma once

#include "atlbase.h"

#define BEGIN_COM_MAP(x)
#define END_COM_MAP()
#define COM_INTERFACE_ENTRY(x)
#define COM_INTERFACE_ENTRY_AGGREGATE(iid, punk)
