"""BIG archive writer and reader.

Layout (mirrors ``StdBIGFileSystem::openArchiveFile``):

    offset 0   "BIGF"
    offset 4   uint32 little endian: size of the whole archive
    offset 8   uint32 big endian:    number of files
    offset 12  uint32 big endian:    offset of the first file (informational)
    offset 16  directory, per file:
                   uint32 big endian offset of the file data (absolute)
                   uint32 big endian size of the file data
                   NUL terminated path (backslash separated)
    ...        file data

The engine lower-cases names itself, so any case is accepted; the pack uses
lower case so the archive is also valid for the OPFS ignore-case layer.
"""

import struct


class BigWriter:
    """Collects files and writes a BIG archive in a deterministic order."""

    def __init__(self):
        self._files = {}

    def add(self, path, data):
        """Add ``data`` (bytes) under ``path`` (any separators; stored with ``\\``)."""
        key = path.replace("/", "\\").lstrip("\\")
        if not key or "\0" in key:
            raise ValueError("bad archive path %r" % path)
        self._files[key] = bytes(data)

    def __len__(self):
        return len(self._files)

    def to_bytes(self):
        names = sorted(self._files, key=lambda n: n.lower())
        directory_size = sum(8 + len(n.encode("ascii")) + 1 for n in names)
        # Two spare bytes after the directory, as the original tools leave.
        first_offset = 16 + directory_size + 2
        entries = []
        offset = first_offset
        for name in names:
            size = len(self._files[name])
            entries.append((name, offset, size))
            offset += size
        total = offset
        out = bytearray()
        out += b"BIGF"
        out += struct.pack("<I", total)
        out += struct.pack(">II", len(names), first_offset)
        for name, off, size in entries:
            out += struct.pack(">II", off, size)
            out += name.encode("ascii") + b"\0"
        out += b"\0\0"
        for name in names:
            out += self._files[name]
        assert len(out) == total
        return bytes(out)

    def write(self, filename):
        with open(filename, "wb") as handle:
            handle.write(self.to_bytes())


def read_big(data):
    """Parse a BIG archive the way the engine does. Returns {lower-case path: bytes}."""
    if data[:4] != b"BIGF":
        raise ValueError("not a BIG archive")
    (count,) = struct.unpack_from(">I", data, 8)
    pos = 0x10
    result = {}
    for _ in range(count):
        offset, size = struct.unpack_from(">II", data, pos)
        pos += 8
        end = data.index(b"\0", pos)
        name = data[pos:end].decode("ascii")
        pos = end + 1
        if offset + size > len(data):
            raise ValueError("entry %s outside archive" % name)
        result[name.lower()] = data[offset:offset + size]
    return result
