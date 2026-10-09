"""BIG archive reader and writer (``BIGF`` and ``BIG4`` headers).

Layout, as ``StdBIGFileSystem::openArchiveFile`` reads it::

    0   "BIGF" (or "BIG4")
    4   uint32 little endian   size of the archive
    8   uint32 big endian      number of files
    12  uint32 big endian      offset of the first file (informational)
    16  directory entries: uint32 big endian offset, uint32 big endian size, NUL terminated path
    ... file data

Paths use backslashes (some tools write slashes); the reader returns them with ``/`` and keeps the
original case. The reader does not load file data until it is asked for it.
"""

import os
import struct


class BigError(ValueError):
    pass


class BigArchive:
    """A read-only view of one BIG file. ``entries`` maps ``path`` (``/`` separated) -> (offset, size)."""

    def __init__(self, path):
        self.path = path
        self.entries = {}
        with open(path, "rb") as handle:
            head = handle.read(16)
            if len(head) < 16 or head[:4] not in (b"BIGF", b"BIG4"):
                raise BigError("%s: not a BIG archive" % path)
            (count,) = struct.unpack(">I", head[8:12])
            if count > 5000000:
                raise BigError("%s: implausible file count %d" % (path, count))
            total = os.fstat(handle.fileno()).st_size
            # read the directory in chunks until `count` entries have been parsed
            buf = b""
            pos = 0
            handle.seek(16)
            for _ in range(count):
                while True:
                    if len(buf) - pos >= 9:
                        end = buf.find(b"\0", pos + 8)
                        if end != -1:
                            break
                    more = handle.read(65536)
                    if not more:
                        raise BigError("%s: truncated directory" % path)
                    buf = buf[pos:] + more
                    pos = 0
                offset, size = struct.unpack_from(">II", buf, pos)
                name = buf[pos + 8:end].decode("latin-1").replace("\\", "/").lstrip("/")
                pos = end + 1
                if offset + size > total:
                    raise BigError("%s: entry %s lies outside the archive" % (path, name))
                self.entries[name] = (offset, size)

    def names(self):
        return list(self.entries)

    def read(self, name):
        offset, size = self.entries[name]
        with open(self.path, "rb") as handle:
            handle.seek(offset)
            return handle.read(size)


class BigWriter:
    """Collects files and writes a BIG archive in a deterministic order (used by tests and fixtures)."""

    def __init__(self):
        self._files = {}

    def add(self, path, data):
        key = path.replace("/", "\\").lstrip("\\")
        if not key or "\0" in key:
            raise ValueError("bad archive path %r" % path)
        self._files[key] = bytes(data)

    def to_bytes(self):
        names = sorted(self._files, key=lambda n: n.lower())
        directory_size = sum(8 + len(n.encode("latin-1")) + 1 for n in names)
        first = 16 + directory_size + 2
        entries = []
        offset = first
        for name in names:
            entries.append((name, offset, len(self._files[name])))
            offset += len(self._files[name])
        out = bytearray(b"BIGF")
        out += struct.pack("<I", offset)
        out += struct.pack(">II", len(names), first)
        for name, off, size in entries:
            out += struct.pack(">II", off, size) + name.encode("latin-1") + b"\0"
        out += b"\0\0"
        for name in names:
            out += self._files[name]
        return bytes(out)

    def write(self, filename):
        with open(filename, "wb") as handle:
            handle.write(self.to_bytes())
