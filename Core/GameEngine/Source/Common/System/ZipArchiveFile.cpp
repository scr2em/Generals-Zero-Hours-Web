/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: ZipArchiveFile.cpp ///////////////////////////////////////////////////
// See ZipArchiveFile.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include <algorithm>
#include <string.h>
#include <zlib.h>

#include "Common/ZipArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/RAMFile.h"
#include "Common/GameMemory.h"
#include "Common/file.h"

namespace
{

const UnsignedInt ZIP_SIG_LOCAL = 0x04034b50;
const UnsignedInt ZIP_SIG_CENTRAL = 0x02014b50;
const UnsignedInt ZIP_SIG_END = 0x06054b50;
const UnsignedInt ZIP_MAX_ENTRY_SIZE = 0x20000000;       // 512 MB: far above any asset
const UnsignedInt ZIP_MAX_DIRECTORY_SIZE = 0x01000000;   // 16 MB of central directory
const Int ZIP_MAX_ENTRIES = 100000;

inline UnsignedInt rd16(const unsigned char *p) { return (UnsignedInt)p[0] | ((UnsignedInt)p[1] << 8); }
inline UnsignedInt rd32(const unsigned char *p) { return rd16(p) | (rd16(p + 2) << 16); }

Bool readAt(File *file, UnsignedInt offset, void *buffer, Int bytes)
{
	if (file->seek((Int)offset, File::START) != (Int)offset)
		return FALSE;
	return file->read(buffer, bytes) == bytes;
}

// Checks one entry name from the central directory. Returns an empty string when fine, else the reason.
const Char *checkName(const AsciiString &name)
{
	const Char *s = name.str();
	if (*s == 0)
		return "empty entry name";
	if (*s == '/')
		return "absolute entry name";
	if (strchr(s, '\\') != nullptr)
		return "back slash in entry name";
	if (s[1] == ':')
		return "drive letter in entry name";

	// every component must be non-empty and not "." or ".."
	const Char *p = s;
	while (TRUE)
	{
		const Char *end = strchr(p, '/');
		Int len = end ? (Int)(end - p) : (Int)strlen(p);
		if (len == 0)
		{
			// a trailing slash (directory entry) is handled by the caller; "a//b" is not allowed
			if (end == nullptr || end[1] != 0)
				return "empty path component in entry name";
		}
		else if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.'))
		{
			return "'.' or '..' in entry name";
		}
		if (end == nullptr)
			break;
		p = end + 1;
		if (*p == 0)
			break;
	}
	return "";
}

bool entryNameLess(const ZipArchiveFile::Entry &a, const ZipArchiveFile::Entry &b)
{
	return strcmp(a.m_name.str(), b.m_name.str()) < 0;
}

} // namespace


ZipArchiveFile::ZipArchiveFile(const AsciiString &name, const AsciiString &path)
	: m_name(name)
	, m_path(path)
{
}

ZipArchiveFile::~ZipArchiveFile()
{
}

//=============================================================================
ZipArchiveFile *ZipArchiveFile::open(const Char *path, AsciiString &error)
{
	error.clear();

	File *fp = TheLocalFileSystem->openFile(path, File::READ | File::BINARY);
	if (fp == nullptr)
	{
		error = "cannot open the file";
		return nullptr;
	}

	const Int fileSize = fp->size();
	if (fileSize < 22)
	{
		fp->close();
		error = "not a zip archive (too small)";
		return nullptr;
	}

	// ---- end of central directory record: in the last 22 + 65535 bytes
	const Int tailSize = fileSize < 22 + 65535 ? fileSize : 22 + 65535;
	unsigned char *tail = NEW unsigned char[tailSize];
	if (!readAt(fp, (UnsignedInt)(fileSize - tailSize), tail, tailSize))
	{
		delete[] tail;
		fp->close();
		error = "cannot read the end of the archive";
		return nullptr;
	}
	Int eocd = -1;
	for (Int i = tailSize - 22; i >= 0; --i)
	{
		if (rd32(tail + i) == ZIP_SIG_END)
		{
			// the comment length must reach exactly to the end of the file
			if ((Int)(i + 22 + rd16(tail + i + 20)) == tailSize)
			{
				eocd = i;
				break;
			}
		}
	}
	if (eocd < 0)
	{
		delete[] tail;
		fp->close();
		error = "not a zip archive (no end record)";
		return nullptr;
	}

	const UnsignedInt diskNo = rd16(tail + eocd + 4);
	const UnsignedInt dirDisk = rd16(tail + eocd + 6);
	const UnsignedInt entriesOnDisk = rd16(tail + eocd + 8);
	const UnsignedInt entryCount = rd16(tail + eocd + 10);
	const UnsignedInt dirSize = rd32(tail + eocd + 12);
	const UnsignedInt dirOffset = rd32(tail + eocd + 16);
	delete[] tail;

	if (diskNo != 0 || dirDisk != 0 || entriesOnDisk != entryCount)
	{
		fp->close();
		error = "multi-part zip archives are not supported";
		return nullptr;
	}
	if (entryCount == 0xFFFF || dirSize == 0xFFFFFFFF || dirOffset == 0xFFFFFFFF)
	{
		fp->close();
		error = "zip64 archives are not supported";
		return nullptr;
	}
	if (dirSize > ZIP_MAX_DIRECTORY_SIZE || dirOffset > (UnsignedInt)fileSize || dirSize > (UnsignedInt)fileSize - dirOffset)
	{
		fp->close();
		error = "corrupt zip archive (central directory out of range)";
		return nullptr;
	}

	// ---- central directory
	unsigned char *dir = NEW unsigned char[dirSize + 1];
	if (dirSize > 0 && !readAt(fp, dirOffset, dir, (Int)dirSize))
	{
		delete[] dir;
		fp->close();
		error = "corrupt zip archive (cannot read the central directory)";
		return nullptr;
	}

	AsciiString lowerPath = path;
	lowerPath.toLower();
	ZipArchiveFile *zip = NEW ZipArchiveFile(path, path);
	zip->m_entries.reserve(entryCount);

	UnsignedInt pos = 0;
	for (UnsignedInt n = 0; n < entryCount; ++n)
	{
		if (pos + 46 > dirSize || rd32(dir + pos) != ZIP_SIG_CENTRAL)
		{
			error = "corrupt zip archive (bad central directory entry)";
			break;
		}
		const unsigned char *h = dir + pos;
		const UnsignedInt flags = rd16(h + 8);
		const UnsignedInt method = rd16(h + 10);
		const UnsignedInt crc = rd32(h + 16);
		const UnsignedInt csize = rd32(h + 20);
		const UnsignedInt usize = rd32(h + 24);
		const UnsignedInt nameLen = rd16(h + 28);
		const UnsignedInt extraLen = rd16(h + 30);
		const UnsignedInt commentLen = rd16(h + 32);
		const UnsignedInt localOffset = rd32(h + 42);
		if (pos + 46 + nameLen + extraLen + commentLen > dirSize)
		{
			error = "corrupt zip archive (central directory entry out of range)";
			break;
		}

		AsciiString name;
		{
			Char *tmp = NEW Char[nameLen + 1];
			memcpy(tmp, h + 46, nameLen);
			tmp[nameLen] = 0;
			if (strlen(tmp) != nameLen)
			{
				delete[] tmp;
				error = "corrupt zip archive (NUL in an entry name)";
				break;
			}
			name = tmp;
			delete[] tmp;
		}
		pos += 46 + nameLen + extraLen + commentLen;

		// directory entries carry no data; skip them
		if (name.getLength() > 0 && name.getCharAt(name.getLength() - 1) == '/')
		{
			continue;
		}

		const Char *nameProblem = checkName(name);
		if (*nameProblem != 0)
		{
			error.format("%s ('%s')", nameProblem, name.str());
			break;
		}
		if ((flags & 1) != 0 || (flags & 0x40) != 0)
		{
			error.format("encrypted entry ('%s')", name.str());
			break;
		}
		if (method != 0 && method != 8)
		{
			error.format("unsupported compression method %u ('%s')", method, name.str());
			break;
		}
		if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF || localOffset == 0xFFFFFFFF)
		{
			error = "zip64 archives are not supported";
			break;
		}
		if (usize > ZIP_MAX_ENTRY_SIZE || (method == 0 && csize != usize))
		{
			error.format("corrupt zip archive (bad size of '%s')", name.str());
			break;
		}

		// the local header must be where the central directory says, and the data must fit before the directory
		unsigned char local[30];
		if (localOffset > dirOffset || dirOffset - localOffset < 30 || !readAt(fp, localOffset, local, 30) || rd32(local) != ZIP_SIG_LOCAL)
		{
			error.format("corrupt zip archive (bad local header of '%s')", name.str());
			break;
		}
		if (rd16(local + 8) != method)
		{
			error.format("corrupt zip archive (header mismatch for '%s')", name.str());
			break;
		}
		const UnsignedInt dataOffset = localOffset + 30 + rd16(local + 26) + rd16(local + 28);
		if (dataOffset > dirOffset || csize > dirOffset - dataOffset)
		{
			error.format("corrupt zip archive ('%s' is cut off)", name.str());
			break;
		}

		Entry e;
		e.m_originalName = name;
		e.m_name = name;
		e.m_name.toLower();
		e.m_dataOffset = dataOffset;
		e.m_compressedSize = csize;
		e.m_size = usize;
		e.m_crc32 = crc;
		e.m_method = (UnsignedShort)method;
		zip->m_entries.push_back(e);
		if ((Int)zip->m_entries.size() > ZIP_MAX_ENTRIES)
		{
			error = "too many entries";
			break;
		}
	}
	delete[] dir;

	if (error.isEmpty())
	{
		std::sort(zip->m_entries.begin(), zip->m_entries.end(), entryNameLess);
		for (size_t i = 1; i < zip->m_entries.size(); ++i)
		{
			if (zip->m_entries[i].m_name == zip->m_entries[i - 1].m_name)
			{
				error.format("two entries named '%s' (names are not case sensitive)", zip->m_entries[i].m_name.str());
				break;
			}
		}
	}

	if (!error.isEmpty())
	{
		fp->close();
		delete zip;
		return nullptr;
	}

	// build the directory tree of the ArchiveFile base class: file info offset = index of the entry
	for (size_t i = 0; i < zip->m_entries.size(); ++i)
	{
		const Entry &e = zip->m_entries[i];
		AsciiString dirPart;
		AsciiString filePart = e.m_name;
		const Char *slash = strrchr(e.m_name.str(), '/');
		if (slash != nullptr)
		{
			filePart = slash + 1;
			Char *tmp = NEW Char[(slash - e.m_name.str()) + 1];
			memcpy(tmp, e.m_name.str(), slash - e.m_name.str());
			tmp[slash - e.m_name.str()] = 0;
			dirPart = tmp;
			delete[] tmp;
		}
		ArchivedFileInfo info;
		info.m_filename = filePart;
		info.m_archiveFilename = lowerPath;
		info.m_offset = (UnsignedInt)i;
		info.m_size = e.m_size;
		zip->addFile(dirPart, &info);
	}

	zip->attachFile(fp);
	return zip;
}

//=============================================================================
const ZipArchiveFile::Entry *ZipArchiveFile::findEntry(const AsciiString &name) const
{
	const ArchivedFileInfo *info = getArchivedFileInfo(name);
	if (info == nullptr || info->m_offset >= m_entries.size())
		return nullptr;
	return &m_entries[info->m_offset];
}

//=============================================================================
Char *ZipArchiveFile::readEntry(const Entry &entry, AsciiString &error) const
{
	error.clear();
	if (m_file == nullptr)
	{
		error = "archive is closed";
		return nullptr;
	}

	Char *out = MSGNEW("RAMFILE") Char[entry.m_size + 1];
	out[entry.m_size] = 0;

	if (entry.m_method == 0)
	{
		if (entry.m_size > 0 && !readAt(m_file, entry.m_dataOffset, out, (Int)entry.m_size))
		{
			delete[] out;
			error.format("cannot read '%s'", entry.m_originalName.str());
			return nullptr;
		}
	}
	else if (entry.m_size > 0)
	{
		// one extra zero byte after the compressed data: the legacy zlib in the tree needs it to finish a raw stream
		unsigned char *in = NEW unsigned char[entry.m_compressedSize + 1];
		in[entry.m_compressedSize] = 0;
		if (entry.m_compressedSize > 0 && !readAt(m_file, entry.m_dataOffset, in, (Int)entry.m_compressedSize))
		{
			delete[] in;
			delete[] out;
			error.format("cannot read '%s'", entry.m_originalName.str());
			return nullptr;
		}

		z_stream strm;
		memset(&strm, 0, sizeof(strm));
		Bool ok = FALSE;
		if (inflateInit2(&strm, -MAX_WBITS) == Z_OK)
		{
			strm.next_in = in;
			strm.avail_in = entry.m_compressedSize + 1;
			strm.next_out = (Bytef *)out;
			strm.avail_out = entry.m_size;
			int rc = inflate(&strm, Z_FINISH);
			// Z_STREAM_END: finished; the legacy zlib may report Z_BUF_ERROR/Z_OK with all output produced
			ok = (rc == Z_STREAM_END || (strm.total_out == entry.m_size && (rc == Z_OK || rc == Z_BUF_ERROR)));
			ok = ok && strm.total_out == entry.m_size;
			inflateEnd(&strm);
		}
		delete[] in;
		if (!ok)
		{
			delete[] out;
			error.format("cannot decompress '%s'", entry.m_originalName.str());
			return nullptr;
		}
	}

	if (entry.m_size > 0)
	{
		uLong crc = crc32(0L, Z_NULL, 0);
		crc = crc32(crc, (const Bytef *)out, entry.m_size);
		if ((UnsignedInt)crc != entry.m_crc32)
		{
			delete[] out;
			error.format("checksum mismatch in '%s'", entry.m_originalName.str());
			return nullptr;
		}
	}
	return out;
}

//=============================================================================
File *ZipArchiveFile::openFile(const Char *filename, Int access)
{
	if ((access & File::WRITE) != 0)
		return nullptr;

	const Entry *entry = findEntry(AsciiString(filename));
	if (entry == nullptr)
		return nullptr;

	AsciiString error;
	Char *data = readEntry(*entry, error);
	if (data == nullptr)
	{
		DEBUG_LOG(("ZipArchiveFile::openFile - %s: %s", m_name.str(), error.str()));
		return nullptr;
	}

	RAMFile *ramFile = newInstance(RAMFile);
	ramFile->deleteOnClose();
	if (ramFile->openFromMemory(entry->m_name, data, (Int)entry->m_size) == FALSE)
	{
		delete[] data;
		ramFile->close();
		return nullptr;
	}
	return ramFile;
}

//=============================================================================
Bool ZipArchiveFile::getFileInfo(const AsciiString &filename, FileInfo *fileInfo) const
{
	const Entry *entry = findEntry(filename);
	if (entry == nullptr)
		return FALSE;

	fileInfo->sizeHigh = 0;
	fileInfo->sizeLow = entry->m_size;
	fileInfo->timestampHigh = m_fileInfo.timestampHigh;
	fileInfo->timestampLow = m_fileInfo.timestampLow;
	return TRUE;
}
