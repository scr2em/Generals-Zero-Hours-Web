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
** WebAssembly port: OLE Automation. BSTRs are real (a length prefix in front
** of a zero terminated UTF-16 string); there are no type libraries and no
** standard dispatch implementation in the browser, so those functions fail
** the way they do on a machine where the type library cannot be found.
*/
#include "webcompat_internal.h"

#include <oaidl.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

BSTR WINAPI SysAllocStringLen(const OLECHAR *pch, UINT cch)
{
	// A 32 bit byte count precedes the characters, as on Windows.
	const size_t bytes = (size_t)cch * sizeof(OLECHAR);
	uint32_t *block = (uint32_t *)malloc(sizeof(uint32_t) + bytes + sizeof(OLECHAR));
	if (block == nullptr)
		return nullptr;
	block[0] = (uint32_t)bytes;
	OLECHAR *str = (OLECHAR *)(block + 1);
	if (pch != nullptr)
		memcpy(str, pch, bytes);
	else
		memset(str, 0, bytes);
	str[cch] = 0;
	return str;
}

BSTR WINAPI SysAllocString(const OLECHAR *psz)
{
	if (psz == nullptr)
		return nullptr;
	UINT len = 0;
	while (psz[len] != 0)
		++len;
	return SysAllocStringLen(psz, len);
}

void WINAPI SysFreeString(BSTR bstrString)
{
	if (bstrString != nullptr)
		free((uint32_t *)bstrString - 1);
}

UINT WINAPI SysStringLen(BSTR bstr)
{
	return bstr != nullptr ? (UINT)(((uint32_t *)bstr)[-1] / sizeof(OLECHAR)) : 0;
}

HRESULT WINAPI LoadTypeLib(const OLECHAR *, ITypeLib **pptlib)
{
	if (pptlib != nullptr)
		*pptlib = nullptr;
	return TYPE_E_CANTLOADLIBRARY;
}

HRESULT WINAPI CreateStdDispatch(IUnknown *, void *, ITypeInfo *, IUnknown **ppunkStdDisp)
{
	if (ppunkStdDisp != nullptr)
		*ppunkStdDisp = nullptr;
	return E_NOTIMPL;
}

} // extern "C"
