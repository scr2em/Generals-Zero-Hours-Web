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
** WebAssembly port: the _bstr_t string wrapper of the compiler COM support.
*/
#pragma once

#include <windows.h>
#include <oaidl.h>

#ifdef __cplusplus

class _bstr_t
{
public:
	_bstr_t() : m_bstr(nullptr) {}
	_bstr_t(const char *s) : m_bstr(nullptr)
	{
		if (s != nullptr)
		{
			const int len = (int)strlen(s);
			m_bstr = SysAllocStringLen(nullptr, len);
			// The characters are Latin-1 here: this wrapper only ever sees file names.
			for (int i = 0; i < len; ++i)
				m_bstr[i] = (OLECHAR)(unsigned char)s[i];
		}
	}
	_bstr_t(const OLECHAR *s) : m_bstr(SysAllocString(s)) {}
	_bstr_t(const _bstr_t &other) : m_bstr(other.m_bstr != nullptr ? SysAllocStringLen(other.m_bstr, SysStringLen(other.m_bstr)) : nullptr) {}
	~_bstr_t() { SysFreeString(m_bstr); }

	_bstr_t &operator=(const _bstr_t &other)
	{
		if (this != &other)
		{
			SysFreeString(m_bstr);
			m_bstr = other.m_bstr != nullptr ? SysAllocStringLen(other.m_bstr, SysStringLen(other.m_bstr)) : nullptr;
		}
		return *this;
	}

	operator const OLECHAR *() const { return m_bstr; }
	operator OLECHAR *() const { return m_bstr; }
	unsigned int length() const { return SysStringLen(m_bstr); }

private:
	BSTR m_bstr;
};

#endif
