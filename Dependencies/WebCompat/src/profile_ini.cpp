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
** WebAssembly port: GetPrivateProfile* / WritePrivateProfileString on .ini
** files.
*/
#include "webcompat_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <string>
#include <vector>

namespace
{

pthread_mutex_t s_iniLock = PTHREAD_MUTEX_INITIALIZER;

struct IniFile
{
	std::vector<std::string> lines;

	bool Load(const char *path)
	{
		lines.clear();
		char resolved[PATH_MAX];
		WebCompat::ResolvePath(path, resolved, sizeof(resolved));
		FILE *file = fopen(resolved, "rb");
		if (!file)
			return false;
		std::string current;
		int c;
		while ((c = fgetc(file)) != EOF)
		{
			if (c == '\n')
			{
				lines.push_back(current);
				current.clear();
			}
			else if (c != '\r')
			{
				current += (char)c;
			}
		}
		if (!current.empty())
			lines.push_back(current);
		fclose(file);
		return true;
	}

	bool Save(const char *path) const
	{
		char resolved[PATH_MAX];
		WebCompat::ResolvePath(path, resolved, sizeof(resolved));
		FILE *file = fopen(resolved, "wb");
		if (!file)
			return false;
		for (size_t i = 0; i < lines.size(); ++i)
			fprintf(file, "%s\r\n", lines[i].c_str());
		fclose(file);
		return true;
	}
};

std::string Trim(const std::string &s)
{
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
		++begin;
	while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t'))
		--end;
	return s.substr(begin, end - begin);
}

// Returns the section name if the line starts a section.
bool ParseSection(const std::string &line, std::string *name)
{
	const std::string trimmed = Trim(line);
	if (trimmed.size() < 2 || trimmed[0] != '[')
		return false;
	const size_t close = trimmed.find(']');
	if (close == std::string::npos)
		return false;
	*name = Trim(trimmed.substr(1, close - 1));
	return true;
}

// Returns the key of a "key=value" line.
bool ParseKey(const std::string &line, std::string *key, std::string *value)
{
	const std::string trimmed = Trim(line);
	if (trimmed.empty() || trimmed[0] == ';')
		return false;
	const size_t equals = trimmed.find('=');
	if (equals == std::string::npos)
		return false;
	*key = Trim(trimmed.substr(0, equals));
	*value = Trim(trimmed.substr(equals + 1));
	return true;
}

// Finds the range [first, last) of the lines of a section, after its header.
bool FindSection(const IniFile &ini, const char *section, size_t *first, size_t *last)
{
	bool inSection = false;
	for (size_t i = 0; i < ini.lines.size(); ++i)
	{
		std::string name;
		if (ParseSection(ini.lines[i], &name))
		{
			if (inSection)
			{
				*last = i;
				return true;
			}
			if (strcasecmp(name.c_str(), section) == 0)
			{
				inSection = true;
				*first = i + 1;
			}
		}
	}
	if (inSection)
		*last = ini.lines.size();
	return inSection;
}

DWORD CopyResult(const std::string &value, LPSTR buffer, DWORD size)
{
	if (size == 0)
		return 0;
	const size_t length = value.size();
	if (length + 1 > size)
	{
		memcpy(buffer, value.c_str(), size - 1);
		buffer[size - 1] = 0;
		return size - 1;
	}
	memcpy(buffer, value.c_str(), length + 1);
	return (DWORD)length;
}

// Returns a list of strings as a multi-string with the final double zero.
DWORD CopyMultiString(const std::vector<std::string> &values, LPSTR buffer, DWORD size)
{
	if (size < 2)
		return 0;
	DWORD used = 0;
	for (size_t i = 0; i < values.size(); ++i)
	{
		if (used + values[i].size() + 2 > size)
		{
			// Truncate: the multi-string still ends with two terminators.
			buffer[size - 2] = 0;
			buffer[size - 1] = 0;
			return size - 2;
		}
		memcpy(buffer + used, values[i].c_str(), values[i].size() + 1);
		used += (DWORD)values[i].size() + 1;
	}
	buffer[used] = 0;
	// The count excludes the terminator of the list.
	return used ? used - 1 : 0;
}

} // namespace

extern "C" {

DWORD WINAPI GetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString, DWORD nSize, LPCSTR lpFileName)
{
	pthread_mutex_lock(&s_iniLock);
	IniFile ini;
	ini.Load(lpFileName);

	std::string result;
	std::vector<std::string> list;
	bool isList = false;

	if (!lpAppName)
	{
		isList = true;
		for (size_t i = 0; i < ini.lines.size(); ++i)
		{
			std::string name;
			if (ParseSection(ini.lines[i], &name))
				list.push_back(name);
		}
	}
	else
	{
		size_t first = 0, last = 0;
		const bool found = FindSection(ini, lpAppName, &first, &last);
		if (!lpKeyName)
		{
			isList = true;
			for (size_t i = first; found && i < last; ++i)
			{
				std::string key, value;
				if (ParseKey(ini.lines[i], &key, &value))
					list.push_back(key);
			}
		}
		else
		{
			bool hasValue = false;
			for (size_t i = first; found && i < last; ++i)
			{
				std::string key, value;
				if (ParseKey(ini.lines[i], &key, &value) && strcasecmp(key.c_str(), lpKeyName) == 0)
				{
					// A value in quotes loses them.
					if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"')
						value = value.substr(1, value.size() - 2);
					result = value;
					hasValue = true;
					break;
				}
			}
			if (!hasValue)
				result = lpDefault ? lpDefault : "";
		}
	}
	pthread_mutex_unlock(&s_iniLock);

	if (isList)
		return CopyMultiString(list, lpReturnedString, nSize);
	return CopyResult(result, lpReturnedString, nSize);
}

UINT WINAPI GetPrivateProfileIntA(LPCSTR lpAppName, LPCSTR lpKeyName, INT nDefault, LPCSTR lpFileName)
{
	char buffer[64];
	GetPrivateProfileStringA(lpAppName, lpKeyName, "", buffer, sizeof(buffer), lpFileName);
	if (!buffer[0])
		return (UINT)nDefault;
	return (UINT)strtol(buffer, nullptr, 10);
}

BOOL WINAPI WritePrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpString, LPCSTR lpFileName)
{
	if (!lpAppName)
		return FALSE;
	pthread_mutex_lock(&s_iniLock);
	IniFile ini;
	ini.Load(lpFileName);

	size_t first = 0, last = 0;
	const bool found = FindSection(ini, lpAppName, &first, &last);

	if (!lpKeyName)
	{
		// Delete the section.
		if (found)
			ini.lines.erase(ini.lines.begin() + (first - 1), ini.lines.begin() + last);
	}
	else
	{
		size_t keyLine = (size_t)-1;
		for (size_t i = first; found && i < last; ++i)
		{
			std::string key, value;
			if (ParseKey(ini.lines[i], &key, &value) && strcasecmp(key.c_str(), lpKeyName) == 0)
			{
				keyLine = i;
				break;
			}
		}
		if (!lpString)
		{
			if (keyLine != (size_t)-1)
				ini.lines.erase(ini.lines.begin() + keyLine);
		}
		else
		{
			const std::string line = std::string(lpKeyName) + "=" + lpString;
			if (keyLine != (size_t)-1)
			{
				ini.lines[keyLine] = line;
			}
			else if (found)
			{
				// After the last key of the section, before trailing blank lines.
				size_t insertAt = last;
				while (insertAt > first && Trim(ini.lines[insertAt - 1]).empty())
					--insertAt;
				ini.lines.insert(ini.lines.begin() + insertAt, line);
			}
			else
			{
				ini.lines.push_back(std::string("[") + lpAppName + "]");
				ini.lines.push_back(line);
			}
		}
	}
	const bool saved = ini.Save(lpFileName);
	pthread_mutex_unlock(&s_iniLock);
	if (!saved)
		SetLastError(ERROR_ACCESS_DENIED);
	return saved ? TRUE : FALSE;
}

} // extern "C"
