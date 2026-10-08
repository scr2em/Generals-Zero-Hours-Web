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
** WebAssembly port: an in-memory Windows registry. It starts with the values
** the game reads at start-up (the install location and language of Command &
** Conquer Generals Zero Hour) and keeps whatever the game writes while it
** runs.
*/
#include "webcompat_internal.h"

#include <string.h>
#include <strings.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Value
{
	std::string name;
	DWORD type;
	std::vector<BYTE> data;
};

struct Key
{
	explicit Key(const std::string &keyName) : name(keyName) {}

	Key *FindChild(const std::string &childName) const
	{
		for (size_t i = 0; i < children.size(); ++i)
		{
			if (strcasecmp(children[i]->name.c_str(), childName.c_str()) == 0)
				return children[i].get();
		}
		return nullptr;
	}

	Value *FindValue(const std::string &valueName)
	{
		for (size_t i = 0; i < values.size(); ++i)
		{
			if (strcasecmp(values[i].name.c_str(), valueName.c_str()) == 0)
				return &values[i];
		}
		return nullptr;
	}

	std::string name;
	std::vector<std::unique_ptr<Key> > children;
	std::vector<Value> values;
};

pthread_mutex_t s_registryLock = PTHREAD_MUTEX_INITIALIZER;

struct Registry
{
	Registry()
		: classesRoot("HKEY_CLASSES_ROOT"), currentUser("HKEY_CURRENT_USER"), localMachine("HKEY_LOCAL_MACHINE"), users("HKEY_USERS")
	{
		Seed();
	}

	Key *Root(HKEY key)
	{
		const uintptr_t value = reinterpret_cast<uintptr_t>(key);
		switch (value)
		{
		case 0x80000000u: return &classesRoot;
		case 0x80000001u: return &currentUser;
		case 0x80000002u: return &localMachine;
		case 0x80000003u: return &users;
		default: return reinterpret_cast<Key *>(key);
		}
	}

	// Follows a backslash separated path from a key. Creates the missing keys
	// if asked to. Returns null if a key is missing.
	Key *Open(Key *start, const char *path, bool create, bool *created = nullptr)
	{
		if (created)
			*created = false;
		Key *current = start;
		if (!path)
			return current;
		std::string rest(path);
		size_t position = 0;
		while (position < rest.size())
		{
			size_t end = rest.find('\\', position);
			if (end == std::string::npos)
				end = rest.size();
			const std::string part = rest.substr(position, end - position);
			position = end + 1;
			if (part.empty())
				continue;
			Key *child = current->FindChild(part);
			if (!child)
			{
				if (!create)
					return nullptr;
				current->children.push_back(std::unique_ptr<Key>(new Key(part)));
				child = current->children.back().get();
				if (created)
					*created = true;
			}
			current = child;
		}
		return current;
	}

	static void SetString(Key *key, const char *name, const char *text)
	{
		Value value;
		value.name = name;
		value.type = REG_SZ;
		value.data.assign(text, text + strlen(text) + 1);
		key->values.push_back(value);
	}

	static void SetDword(Key *key, const char *name, DWORD number)
	{
		Value value;
		value.name = name;
		value.type = REG_DWORD;
		value.data.assign(reinterpret_cast<BYTE *>(&number), reinterpret_cast<BYTE *>(&number) + sizeof(number));
		key->values.push_back(value);
	}

	void Seed()
	{
		// Zero Hour is installed in /game, the original game, whose data Zero
		// Hour also reads, in /generals.
		static const struct { const char *key; const char *installPath; } games[] = {
			{ "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", "/game/" },
			{ "SOFTWARE\\Electronic Arts\\EA Games\\Generals", "/generals/" },
		};
		for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); ++i)
		{
			Key *key = Open(&localMachine, games[i].key, true);
			SetString(key, "InstallPath", games[i].installPath);
			SetString(key, "Language", "english");
			SetDword(key, "Version", 0x00010004);
			SetDword(key, "MapPackVersion", 0x00010000);
		}
	}

	Key classesRoot;
	Key currentUser;
	Key localMachine;
	Key users;
};

Registry &GetRegistry()
{
	static Registry registry;
	return registry;
}

struct RegistryLock
{
	RegistryLock() { pthread_mutex_lock(&s_registryLock); }
	~RegistryLock() { pthread_mutex_unlock(&s_registryLock); }
};

// Copies a value out for RegQueryValueEx / RegEnumValue. The data of a string
// is always handed out with a terminator, as Windows does.
LONG CopyValueOut(const Value &value, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
	if (lpType)
		*lpType = value.type;
	size_t size = value.data.size();
	const bool isString = value.type == REG_SZ || value.type == REG_EXPAND_SZ;
	const bool needsTerminator = isString && (size == 0 || value.data[size - 1] != 0);
	if (needsTerminator)
		++size;
	if (!lpcbData)
		return lpData ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
	if (!lpData)
	{
		*lpcbData = (DWORD)size;
		return ERROR_SUCCESS;
	}
	if (*lpcbData < size)
	{
		*lpcbData = (DWORD)size;
		return ERROR_MORE_DATA;
	}
	if (!value.data.empty())
		memcpy(lpData, &value.data[0], value.data.size());
	if (needsTerminator)
		lpData[size - 1] = 0;
	*lpcbData = (DWORD)size;
	return ERROR_SUCCESS;
}

std::string NarrowName(LPCWSTR name)
{
	std::string result;
	for (; name && *name; ++name)
		result += *name < 0x80 ? (char)*name : '?';
	return result;
}

} // namespace

extern "C" {

LONG WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD, REGSAM, PHKEY phkResult)
{
	RegistryLock lock;
	Registry &registry = GetRegistry();
	Key *start = registry.Root(hKey);
	if (!start || !phkResult)
		return ERROR_INVALID_HANDLE;
	Key *key = registry.Open(start, lpSubKey, false);
	if (!key)
		return ERROR_FILE_NOT_FOUND;
	*phkResult = reinterpret_cast<HKEY>(key);
	return ERROR_SUCCESS;
}

LONG WINAPI RegOpenKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult)
{
	return RegOpenKeyExA(hKey, lpSubKey, 0, KEY_READ, phkResult);
}

LONG WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY phkResult, LPDWORD lpdwDisposition)
{
	RegistryLock lock;
	Registry &registry = GetRegistry();
	Key *start = registry.Root(hKey);
	if (!start || !phkResult)
		return ERROR_INVALID_HANDLE;
	bool created;
	Key *key = registry.Open(start, lpSubKey, true, &created);
	*phkResult = reinterpret_cast<HKEY>(key);
	if (lpdwDisposition)
		*lpdwDisposition = created ? REG_CREATED_NEW_KEY : REG_OPENED_EXISTING_KEY;
	return ERROR_SUCCESS;
}

LONG WINAPI RegCreateKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult)
{
	return RegCreateKeyExA(hKey, lpSubKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, phkResult, nullptr);
}

LONG WINAPI RegCloseKey(HKEY)
{
	// Keys live as long as the registry does.
	return ERROR_SUCCESS;
}

LONG WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	Value *value = key->FindValue(lpValueName ? lpValueName : "");
	if (!value)
		return ERROR_FILE_NOT_FOUND;
	return CopyValueOut(*value, lpType, lpData, lpcbData);
}

LONG WINAPI RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	Value *value = key->FindValue(NarrowName(lpValueName));
	if (!value)
		return ERROR_FILE_NOT_FOUND;
	if (value->type != REG_SZ && value->type != REG_EXPAND_SZ)
		return CopyValueOut(*value, lpType, lpData, lpcbData);

	// Strings are stored narrow and handed out wide.
	Value wide;
	wide.type = value->type;
	for (size_t i = 0; i < value->data.size(); ++i)
	{
		const WCHAR c = value->data[i];
		wide.data.push_back((BYTE)(c & 0xFF));
		wide.data.push_back((BYTE)(c >> 8));
	}
	if (wide.data.size() < 2 || wide.data[wide.data.size() - 2] != 0 || wide.data[wide.data.size() - 1] != 0)
	{
		wide.data.push_back(0);
		wide.data.push_back(0);
	}
	return CopyValueOut(wide, lpType, lpData, lpcbData);
}

LONG WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD, DWORD dwType, const BYTE *lpData, DWORD cbData)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	const std::string name = lpValueName ? lpValueName : "";
	Value *value = key->FindValue(name);
	if (!value)
	{
		key->values.push_back(Value());
		value = &key->values.back();
		value->name = name;
	}
	value->type = dwType;
	value->data.assign(lpData, lpData + (lpData ? cbData : 0));
	return ERROR_SUCCESS;
}

LONG WINAPI RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData)
{
	const std::string name = NarrowName(lpValueName);
	if (dwType != REG_SZ && dwType != REG_EXPAND_SZ)
		return RegSetValueExA(hKey, name.c_str(), Reserved, dwType, lpData, cbData);
	// Strings are stored narrow.
	std::vector<BYTE> narrow;
	const WCHAR *wide = reinterpret_cast<const WCHAR *>(lpData);
	for (DWORD i = 0; i < cbData / sizeof(WCHAR); ++i)
		narrow.push_back(wide[i] < 0x80 ? (BYTE)wide[i] : (BYTE)'?');
	return RegSetValueExA(hKey, name.c_str(), Reserved, dwType, narrow.empty() ? nullptr : &narrow[0], (DWORD)narrow.size());
}

LONG WINAPI RegDeleteValueA(HKEY hKey, LPCSTR lpValueName)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	const std::string name = lpValueName ? lpValueName : "";
	for (size_t i = 0; i < key->values.size(); ++i)
	{
		if (strcasecmp(key->values[i].name.c_str(), name.c_str()) == 0)
		{
			key->values.erase(key->values.begin() + i);
			return ERROR_SUCCESS;
		}
	}
	return ERROR_FILE_NOT_FOUND;
}

LONG WINAPI RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey)
{
	RegistryLock lock;
	Registry &registry = GetRegistry();
	Key *start = registry.Root(hKey);
	if (!start || !lpSubKey || !*lpSubKey)
		return ERROR_INVALID_HANDLE;
	// Find the parent of the key to delete.
	std::string path(lpSubKey);
	std::string leaf = path;
	Key *parent = start;
	const size_t slash = path.find_last_of('\\');
	if (slash != std::string::npos)
	{
		leaf = path.substr(slash + 1);
		parent = registry.Open(start, path.substr(0, slash).c_str(), false);
	}
	if (!parent)
		return ERROR_FILE_NOT_FOUND;
	for (size_t i = 0; i < parent->children.size(); ++i)
	{
		if (strcasecmp(parent->children[i]->name.c_str(), leaf.c_str()) == 0)
		{
			if (!parent->children[i]->children.empty())
				return ERROR_ACCESS_DENIED; // a key with subkeys cannot be deleted
			parent->children.erase(parent->children.begin() + i);
			return ERROR_SUCCESS;
		}
	}
	return ERROR_FILE_NOT_FOUND;
}

LONG WINAPI RegEnumValueA(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	if (dwIndex >= key->values.size())
		return ERROR_NO_MORE_ITEMS;
	const Value &value = key->values[dwIndex];
	if (!lpValueName || !lpcchValueName || *lpcchValueName < value.name.size() + 1)
	{
		if (lpcchValueName)
			*lpcchValueName = (DWORD)value.name.size() + 1;
		return ERROR_MORE_DATA;
	}
	memcpy(lpValueName, value.name.c_str(), value.name.size() + 1);
	*lpcchValueName = (DWORD)value.name.size();
	return CopyValueOut(value, lpType, lpData, lpcbData);
}

LONG WINAPI RegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName, LPDWORD, LPSTR, LPDWORD, PFILETIME lpftLastWriteTime)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	if (dwIndex >= key->children.size())
		return ERROR_NO_MORE_ITEMS;
	const std::string &name = key->children[dwIndex]->name;
	if (!lpName || !lpcchName || *lpcchName < name.size() + 1)
	{
		if (lpcchName)
			*lpcchName = (DWORD)name.size() + 1;
		return ERROR_MORE_DATA;
	}
	memcpy(lpName, name.c_str(), name.size() + 1);
	*lpcchName = (DWORD)name.size();
	if (lpftLastWriteTime)
		GetSystemTimeAsFileTime(lpftLastWriteTime);
	return ERROR_SUCCESS;
}

LONG WINAPI RegEnumKeyA(HKEY hKey, DWORD dwIndex, LPSTR lpName, DWORD cchName)
{
	DWORD size = cchName;
	return RegEnumKeyExA(hKey, dwIndex, lpName, &size, nullptr, nullptr, nullptr, nullptr);
}

LONG WINAPI RegQueryInfoKeyA(HKEY hKey, LPSTR lpClass, LPDWORD lpcchClass, LPDWORD, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime)
{
	RegistryLock lock;
	Key *key = GetRegistry().Root(hKey);
	if (!key)
		return ERROR_INVALID_HANDLE;
	if (lpClass && lpcchClass && *lpcchClass)
		lpClass[0] = 0;
	if (lpcchClass)
		*lpcchClass = 0;
	DWORD maxSubKeyLength = 0;
	for (size_t i = 0; i < key->children.size(); ++i)
		maxSubKeyLength = (DWORD)std::max<size_t>(maxSubKeyLength, key->children[i]->name.size());
	DWORD maxNameLength = 0;
	DWORD maxValueLength = 0;
	for (size_t i = 0; i < key->values.size(); ++i)
	{
		maxNameLength = (DWORD)std::max<size_t>(maxNameLength, key->values[i].name.size());
		maxValueLength = (DWORD)std::max<size_t>(maxValueLength, key->values[i].data.size());
	}
	if (lpcSubKeys) *lpcSubKeys = (DWORD)key->children.size();
	if (lpcbMaxSubKeyLen) *lpcbMaxSubKeyLen = maxSubKeyLength;
	if (lpcbMaxClassLen) *lpcbMaxClassLen = 0;
	if (lpcValues) *lpcValues = (DWORD)key->values.size();
	if (lpcbMaxValueNameLen) *lpcbMaxValueNameLen = maxNameLength;
	if (lpcbMaxValueLen) *lpcbMaxValueLen = maxValueLength;
	if (lpcbSecurityDescriptor) *lpcbSecurityDescriptor = 0;
	if (lpftLastWriteTime) GetSystemTimeAsFileTime(lpftLastWriteTime);
	return ERROR_SUCCESS;
}

LONG WINAPI RegFlushKey(HKEY)
{
	return ERROR_SUCCESS;
}

} // extern "C"
