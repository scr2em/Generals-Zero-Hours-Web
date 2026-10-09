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
** WebAssembly port: Video for Windows. There is no AVI writer in the browser,
** so no file can be opened and no stream created.
*/
#include <vfw.h>

extern "C" {

void WINAPI AVIFileInit(void)
{
}

void WINAPI AVIFileExit(void)
{
}

HRESULT WINAPI AVIFileOpenA(PAVIFILE *ppfile, LPCSTR, UINT, LPCLSID)
{
	if (ppfile != nullptr)
		*ppfile = nullptr;
	return AVIERR_FILEOPEN;
}

HRESULT WINAPI AVIFileCreateStreamA(PAVIFILE, PAVISTREAM *ppavi, AVISTREAMINFO *)
{
	if (ppavi != nullptr)
		*ppavi = nullptr;
	return AVIERR_UNSUPPORTED;
}

ULONG WINAPI AVIFileRelease(PAVIFILE)
{
	return 0;
}

HRESULT WINAPI AVIStreamSetFormat(PAVISTREAM, LONG, LPVOID, LONG)
{
	return AVIERR_UNSUPPORTED;
}

HRESULT WINAPI AVIStreamWrite(PAVISTREAM, LONG, LONG, LPVOID, LONG, DWORD, LONG *, LONG *)
{
	return AVIERR_UNSUPPORTED;
}

ULONG WINAPI AVIStreamRelease(PAVISTREAM)
{
	return 0;
}

} // extern "C"
