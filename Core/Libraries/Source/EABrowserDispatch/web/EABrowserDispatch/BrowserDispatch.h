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
** WebAssembly port: what MIDL generates from BrowserDispatch.idl on Windows.
*/
#pragma once

#include <objbase.h>

extern "C" const IID IID_IBrowserDispatch;

interface IBrowserDispatch : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE TestMethod(int num1) = 0;
};
