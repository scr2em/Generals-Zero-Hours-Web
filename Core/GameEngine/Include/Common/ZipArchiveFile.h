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

// FILE: ZipArchiveFile.h /////////////////////////////////////////////////////
//
// A ZIP archive as an ArchiveFile, next to the BIG archives. It is the container of the
// army packages (.zharmy, see docs/ARMY_PACKAGES.md), but knows nothing about them.
//
// Supported: stored and deflated entries, UTF-8 names, any entry order. Not supported
// (the archive is refused): encryption, ZIP64, other compression methods, names with
// "..", absolute paths or back slashes, two entries with the same name (ignoring case).
// Entry names are matched case-insensitively, like game files.
//
// The central directory and every local header are checked when the archive is opened,
// so a damaged or cut off archive is refused up front. The data of an entry is checked
// against its CRC when the entry is read.
//
// Uses only the engine's file abstraction (TheLocalFileSystem) and zlib, so it works the
// same on every platform.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"

class ZipArchiveFile : public ArchiveFile
{
public:
	struct Entry
	{
		AsciiString m_name;            ///< lower case, '/' separators, as stored in the archive
		AsciiString m_originalName;    ///< as stored
		UnsignedInt m_dataOffset;      ///< offset of the (compressed) data in the archive file
		UnsignedInt m_compressedSize;
		UnsignedInt m_size;            ///< uncompressed size
		UnsignedInt m_crc32;
		UnsignedShort m_method;        ///< 0 stored, 8 deflate
	};

	/// Opens and checks the archive. Returns nullptr and fills error (a short plain sentence) when it is not usable.
	static ZipArchiveFile *open(const Char *path, AsciiString &error);

	virtual ~ZipArchiveFile() override;

	// ArchiveFile interface
	virtual Bool					getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const override;
	virtual File*					openFile(const Char *filename, Int access = 0) override;
	virtual void					closeAllFiles() override {}
	virtual AsciiString		getName() override { return m_name; }
	virtual AsciiString		getPath() override { return m_path; }
	virtual void					setSearchPriority(Int new_priority) override {}
	virtual void					close() override {}

	/// The entries, sorted by lower case name.
	const std::vector<Entry> &getEntries() const { return m_entries; }

	/// Finds an entry by name (case-insensitive, '/' or '\\' separators). nullptr if absent.
	const Entry *findEntry(const AsciiString &name) const;

	/// Reads an entry completely and checks its CRC. The buffer is allocated with MSGNEW("RAMFILE") Char[size + 1]
	/// (one extra zero byte, so text can be used as a C string) and is owned by the caller (delete[]).
	/// Returns nullptr and fills error on failure.
	Char *readEntry(const Entry &entry, AsciiString &error) const;

private:
	ZipArchiveFile(const AsciiString &name, const AsciiString &path);

	AsciiString m_name;
	AsciiString m_path;
	std::vector<Entry> m_entries;
};
