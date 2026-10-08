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
** WebAssembly port: std::basic_string<wchar_t> for 16-bit wchar_t.
**
** The game is built with -fshort-wchar, but the prebuilt libc++ was compiled
** with a 32-bit wchar_t. libc++ declares its std::wstring members as extern
** templates, so without this file calls would resolve to the 32-bit
** instantiations in libc++.a. The explicit instantiation below provides
** 16-bit versions of the same symbols; this object is linked before libc++,
** so the linker keeps these definitions.
*/

#include <string>

template class std::basic_string<wchar_t>;
