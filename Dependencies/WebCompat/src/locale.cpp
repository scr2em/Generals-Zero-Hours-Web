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
** WebAssembly port: code page conversion and the Win32 string and locale
** functions. The ANSI code page is Windows-1252.
*/
#include "webcompat_internal.h"
#include "charset.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>

using namespace WebCompat;

extern "C" {

int WINAPI MultiByteToWideChar(UINT CodePage, DWORD, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
{
	if (!lpMultiByteStr || cchWideChar < 0 || (cchWideChar > 0 && !lpWideCharStr))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const unsigned char *source = reinterpret_cast<const unsigned char *>(lpMultiByteStr);
	size_t sourceLength = cbMultiByte < 0 ? strlen(lpMultiByteStr) + 1 : (size_t)cbMultiByte;

	int count = 0;
	bool overflow = false;
	auto put = [&](uint16_t c)
	{
		if (cchWideChar > 0)
		{
			if (count < cchWideChar)
				lpWideCharStr[count] = c;
			else
				overflow = true;
		}
		++count;
	};

	if (CodePage == CP_UTF8)
	{
		size_t i = 0;
		while (i < sourceLength)
		{
			uint32_t codePoint;
			const size_t used = DecodeUtf8(source + i, sourceLength - i, &codePoint);
			if (used == 0)
			{
				put(0xFFFD);
				++i;
				continue;
			}
			i += used;
			if (codePoint >= 0x10000)
			{
				codePoint -= 0x10000;
				put((uint16_t)(0xD800 + (codePoint >> 10)));
				put((uint16_t)(0xDC00 + (codePoint & 0x3FF)));
			}
			else
			{
				put((uint16_t)codePoint);
			}
		}
	}
	else
	{
		for (size_t i = 0; i < sourceLength; ++i)
			put(Cp1252ToUtf16(source[i]));
	}

	if (overflow)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	return count;
}

int WINAPI WideCharToMultiByte(UINT CodePage, DWORD, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
	if (!lpWideCharStr || cbMultiByte < 0 || (cbMultiByte > 0 && !lpMultiByteStr))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	size_t sourceLength;
	if (cchWideChar < 0)
	{
		sourceLength = 0;
		while (lpWideCharStr[sourceLength])
			++sourceLength;
		++sourceLength;
	}
	else
	{
		sourceLength = (size_t)cchWideChar;
	}

	int count = 0;
	bool overflow = false;
	bool usedDefault = false;
	auto put = [&](unsigned char c)
	{
		if (cbMultiByte > 0)
		{
			if (count < cbMultiByte)
				lpMultiByteStr[count] = (char)c;
			else
				overflow = true;
		}
		++count;
	};
	const unsigned char defaultChar = lpDefaultChar ? (unsigned char)lpDefaultChar[0] : '?';

	for (size_t i = 0; i < sourceLength; ++i)
	{
		uint32_t c = lpWideCharStr[i];
		if (CodePage == CP_UTF8)
		{
			if (IsHighSurrogate(c) && i + 1 < sourceLength && IsLowSurrogate(lpWideCharStr[i + 1]))
			{
				c = 0x10000 + ((c - 0xD800) << 10) + (lpWideCharStr[i + 1] - 0xDC00);
				++i;
			}
			else if (IsHighSurrogate(c) || IsLowSurrogate(c))
			{
				c = 0xFFFD;
			}
			unsigned char bytes[4];
			const size_t length = EncodeUtf8(c, bytes);
			for (size_t b = 0; b < length; ++b)
				put(bytes[b]);
		}
		else
		{
			const int mapped = Utf16ToCp1252(c);
			if (mapped < 0)
			{
				put(defaultChar);
				usedDefault = true;
			}
			else
			{
				put((unsigned char)mapped);
			}
		}
	}

	if (lpUsedDefaultChar)
		*lpUsedDefaultChar = usedDefault;
	if (overflow)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	return count;
}

int WINAPIV wsprintfA(LPSTR lpOut, LPCSTR lpFmt, ...)
{
	va_list args;
	va_start(args, lpFmt);
	const int result = vsprintf(lpOut, lpFmt, args);
	va_end(args);
	return result;
}

int WINAPI wvsprintfA(LPSTR lpOutput, LPCSTR lpFmt, va_list arglist)
{
	return vsprintf(lpOutput, lpFmt, arglist);
}

int WINAPIV wsprintfW(LPWSTR lpOut, LPCWSTR lpFmt, ...)
{
	va_list args;
	va_start(args, lpFmt);
	const int result = _vsnwprintf(lpOut, 1024, lpFmt, args);
	va_end(args);
	if (result < 0)
		lpOut[1023] = 0;
	return result;
}

LPSTR WINAPI lstrcpyA(LPSTR lpString1, LPCSTR lpString2)
{
	return strcpy(lpString1, lpString2);
}

LPSTR WINAPI lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength)
{
	if (iMaxLength <= 0)
		return lpString1;
	strncpy(lpString1, lpString2, (size_t)iMaxLength - 1);
	lpString1[iMaxLength - 1] = 0;
	return lpString1;
}

LPSTR WINAPI lstrcatA(LPSTR lpString1, LPCSTR lpString2)
{
	return strcat(lpString1, lpString2);
}

int WINAPI lstrlenA(LPCSTR lpString)
{
	return lpString ? (int)strlen(lpString) : 0;
}

int WINAPI lstrcmpA(LPCSTR lpString1, LPCSTR lpString2)
{
	return strcmp(lpString1, lpString2);
}

int WINAPI lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2)
{
	return strcasecmp(lpString1, lpString2);
}

int WINAPI lstrlenW(LPCWSTR lpString)
{
	return lpString ? (int)wcslen(lpString) : 0;
}

LPWSTR WINAPI lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2)
{
	return wcscpy(lpString1, lpString2);
}

LPWSTR WINAPI lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength)
{
	if (iMaxLength <= 0)
		return lpString1;
	wcsncpy(lpString1, lpString2, (size_t)iMaxLength - 1);
	lpString1[iMaxLength - 1] = 0;
	return lpString1;
}

LPWSTR WINAPI lstrcatW(LPWSTR lpString1, LPCWSTR lpString2)
{
	return wcscat(lpString1, lpString2);
}

int WINAPI lstrcmpiW(LPCWSTR lpString1, LPCWSTR lpString2)
{
	return _wcsicmp(lpString1, lpString2);
}

LPSTR WINAPI CharUpperA(LPSTR lpsz)
{
	if (reinterpret_cast<uintptr_t>(lpsz) <= 0xFFFF)
	{
		// A single character is passed in the low word.
		return reinterpret_cast<LPSTR>((uintptr_t)toupper((unsigned char)(uintptr_t)lpsz));
	}
	return _strupr(lpsz);
}

LPSTR WINAPI CharLowerA(LPSTR lpsz)
{
	if (reinterpret_cast<uintptr_t>(lpsz) <= 0xFFFF)
		return reinterpret_cast<LPSTR>((uintptr_t)tolower((unsigned char)(uintptr_t)lpsz));
	return _strlwr(lpsz);
}

LPSTR WINAPI CharNextA(LPCSTR lpsz)
{
	return const_cast<LPSTR>(*lpsz ? lpsz + 1 : lpsz);
}

LPSTR WINAPI CharPrevA(LPCSTR lpszStart, LPCSTR lpszCurrent)
{
	return const_cast<LPSTR>(lpszCurrent > lpszStart ? lpszCurrent - 1 : lpszStart);
}

BOOL WINAPI IsCharAlphaA(CHAR ch)
{
	return isalpha((unsigned char)ch) != 0 || ((unsigned char)ch >= 0xC0 && (unsigned char)ch != 0xD7 && (unsigned char)ch != 0xF7);
}

BOOL WINAPI IsCharAlphaNumericA(CHAR ch)
{
	return IsCharAlphaA(ch) || isdigit((unsigned char)ch);
}

BOOL WINAPI IsDBCSLeadByte(BYTE)
{
	return FALSE; // single byte code page
}

int WINAPI CompareStringA(LCID, DWORD dwCmpFlags, LPCSTR lpString1, int cchCount1, LPCSTR lpString2, int cchCount2)
{
	const size_t length1 = cchCount1 < 0 ? strlen(lpString1) : (size_t)cchCount1;
	const size_t length2 = cchCount2 < 0 ? strlen(lpString2) : (size_t)cchCount2;
	const size_t common = length1 < length2 ? length1 : length2;
	for (size_t i = 0; i < common; ++i)
	{
		int a = (unsigned char)lpString1[i];
		int b = (unsigned char)lpString2[i];
		if (dwCmpFlags & NORM_IGNORECASE)
		{
			a = tolower(a);
			b = tolower(b);
		}
		if (a != b)
			return a < b ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
	}
	if (length1 == length2)
		return CSTR_EQUAL;
	return length1 < length2 ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
}

BOOL WINAPI GetStringTypeExW(LCID, DWORD dwInfoType, LPCWSTR lpSrcStr, int cchSrc, LPWORD lpCharType)
{
	if (dwInfoType != CT_CTYPE1 || !lpSrcStr || !lpCharType)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	const int count = cchSrc < 0 ? (int)wcslen(lpSrcStr) + 1 : cchSrc;
	for (int i = 0; i < count; ++i)
	{
		const wint_t c = lpSrcStr[i];
		WORD type = 0;
		if (iswupper(c)) type |= C1_UPPER;
		if (iswlower(c)) type |= C1_LOWER;
		if (iswdigit(c)) type |= C1_DIGIT;
		if (iswspace(c)) type |= C1_SPACE;
		if (iswpunct(c)) type |= C1_PUNCT;
		if (iswcntrl(c)) type |= C1_CNTRL;
		if (c == L' ' || c == L'\t') type |= C1_BLANK;
		if (iswxdigit(c)) type |= C1_XDIGIT;
		if (iswalpha(c)) type |= C1_ALPHA;
		lpCharType[i] = type;
	}
	return TRUE;
}

int WINAPI GetLocaleInfoA(LCID, DWORD LCType, LPSTR lpLCData, int cchData)
{
	const char *value;
	switch (LCType & 0xFFFF)
	{
	case 0x1001: value = "English"; break; // LOCALE_SENGLANGUAGE
	case 0x0003: value = "ENU"; break;     // LOCALE_SABBREVLANGNAME
	case 0x1004: value = "1252"; break;    // LOCALE_IDEFAULTANSICODEPAGE
	default: value = ""; break;
	}
	const int length = (int)strlen(value) + 1;
	if (cchData == 0)
		return length;
	if (cchData < length)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	memcpy(lpLCData, value, (size_t)length);
	return length;
}

} // extern "C"
