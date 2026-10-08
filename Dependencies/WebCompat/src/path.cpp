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
** WebAssembly port: resolution of Windows-style paths in the POSIX file
** system. The game's data uses backslashes and the capitalisation of a
** case-insensitive file system, the Emscripten file systems have forward
** slashes and are case-sensitive.
*/
#include "webcompat_internal.h"

#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace
{

pthread_mutex_t s_cacheLock = PTHREAD_MUTEX_INITIALIZER;

// Resolved paths by the lower case path that was asked for. A cached path is
// only trusted while it still exists.
std::unordered_map<std::string, std::string> &Cache()
{
	static std::unordered_map<std::string, std::string> cache;
	return cache;
}

bool Exists(const std::string &path)
{
	return access(path.c_str(), F_OK) == 0;
}

// Finds the entry of a directory that matches the name ignoring case.
bool FindCaseInsensitive(const std::string &directory, const std::string &name, std::string *found)
{
	DIR *dir = opendir(directory.empty() ? "." : directory.c_str());
	if (!dir)
		return false;
	bool result = false;
	while (const struct dirent *entry = readdir(dir))
	{
		if (strcasecmp(entry->d_name, name.c_str()) == 0)
		{
			*found = entry->d_name;
			result = true;
			break;
		}
	}
	closedir(dir);
	return result;
}

std::string Join(const std::string &directory, const std::string &name)
{
	if (directory.empty())
		return name;
	if (directory == "/")
		return "/" + name;
	return directory + "/" + name;
}

std::string LowerCase(const std::string &s)
{
	std::string result(s);
	for (size_t i = 0; i < result.size(); ++i)
		result[i] = (char)tolower((unsigned char)result[i]);
	return result;
}

} // namespace

namespace WebCompat
{

bool ResolvePath(const char *path, char *resolved, size_t resolvedSize)
{
	if (!path || !resolved || resolvedSize == 0)
		return false;

	// Normalise the separators and drop the drive letter.
	std::string normalized(path);
	for (size_t i = 0; i < normalized.size(); ++i)
	{
		if (normalized[i] == '\\')
			normalized[i] = '/';
	}
	if (normalized.size() >= 2 && normalized[1] == ':' && isalpha((unsigned char)normalized[0]))
		normalized.erase(0, 2);

	bool exists = false;
	std::string result;

	if (normalized.empty())
	{
		result = normalized;
	}
	else if (Exists(normalized))
	{
		result = normalized;
		exists = true;
	}
	else
	{
		const std::string key = LowerCase(normalized);
		pthread_mutex_lock(&s_cacheLock);
		std::unordered_map<std::string, std::string>::iterator it = Cache().find(key);
		if (it != Cache().end())
		{
			if (Exists(it->second))
			{
				result = it->second;
				exists = true;
			}
			else
			{
				Cache().erase(it);
			}
		}
		pthread_mutex_unlock(&s_cacheLock);

		if (!exists)
		{
			// Walk the path and match every component.
			std::string current;
			size_t position = 0;
			bool missing = false;
			if (!normalized.empty() && normalized[0] == '/')
			{
				current = "/";
				position = 1;
			}
			while (position <= normalized.size())
			{
				size_t end = normalized.find('/', position);
				if (end == std::string::npos)
					end = normalized.size();
				const std::string component = normalized.substr(position, end - position);
				position = end + 1;
				if (component.empty() || component == ".")
					continue;

				if (missing)
				{
					current = Join(current, component);
					continue;
				}
				const std::string candidate = Join(current, component);
				std::string match;
				if (component == ".." || Exists(candidate))
					current = candidate;
				else if (FindCaseInsensitive(current, component, &match))
					current = Join(current, match);
				else
				{
					current = candidate;
					missing = true;
				}
			}
			// A trailing separator names a directory.
			if (!normalized.empty() && normalized[normalized.size() - 1] == '/' && !current.empty() && current[current.size() - 1] != '/')
				current += "/";
			result = current;
			exists = !missing;

			if (exists)
			{
				pthread_mutex_lock(&s_cacheLock);
				Cache()[key] = result;
				pthread_mutex_unlock(&s_cacheLock);
			}
		}
	}

	if (result.size() + 1 > resolvedSize)
	{
		resolved[0] = 0;
		return false;
	}
	memcpy(resolved, result.c_str(), result.size() + 1);
	return exists;
}

} // namespace WebCompat

extern "C" int webcompat_resolve_path(const char *path, char *resolved, size_t resolvedSize)
{
	return WebCompat::ResolvePath(path, resolved, resolvedSize) ? 1 : 0;
}
