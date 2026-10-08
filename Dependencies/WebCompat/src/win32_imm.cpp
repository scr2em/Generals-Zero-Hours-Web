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
** WebAssembly port: the Input Method Manager. The page's text input is
** already composed by the browser, so there is no input context and the
** composition and candidate queries are empty.
*/
#include <imm.h>

extern "C" {

HIMC WINAPI ImmGetContext(HWND)
{
	return nullptr;
}

BOOL WINAPI ImmReleaseContext(HWND, HIMC)
{
	return TRUE;
}

HIMC WINAPI ImmCreateContext(void)
{
	return nullptr;
}

BOOL WINAPI ImmDestroyContext(HIMC)
{
	return FALSE;
}

HIMC WINAPI ImmAssociateContext(HWND, HIMC)
{
	return nullptr;
}

HWND WINAPI ImmGetDefaultIMEWnd(HWND)
{
	return nullptr;
}

BOOL WINAPI ImmGetConversionStatus(HIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
	if (lpfdwConversion)
		*lpfdwConversion = IME_CMODE_ALPHANUMERIC;
	if (lpfdwSentence)
		*lpfdwSentence = IME_SMODE_NONE;
	return FALSE;
}

DWORD WINAPI ImmGetProperty(HKL, DWORD)
{
	return 0;
}

LONG WINAPI ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD)
{
	return 0;
}

LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD)
{
	return 0;
}

DWORD WINAPI ImmGetCandidateListCountA(HIMC, LPDWORD lpdwListCount)
{
	if (lpdwListCount)
		*lpdwListCount = 0;
	return 0;
}

DWORD WINAPI ImmGetCandidateListCountW(HIMC, LPDWORD lpdwListCount)
{
	if (lpdwListCount)
		*lpdwListCount = 0;
	return 0;
}

DWORD WINAPI ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD)
{
	return 0;
}

DWORD WINAPI ImmGetCandidateListW(HIMC, DWORD, LPCANDIDATELIST, DWORD)
{
	return 0;
}

} // extern "C"
