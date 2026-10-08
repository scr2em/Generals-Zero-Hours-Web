"""DataChunk writer and reader: the container format of ``.map`` files and ``.scb`` script files.

Mirrors ``DataChunkOutput`` / ``DataChunkInput`` (GameEngine/Source/Common/System/DataChunk.cpp)::

    "CkMp"  int32 count   then per symbol: uint8 len, name bytes, uint32 id      (table of contents)
    chunks: uint32 id, uint16 version, int32 dataSize, <data>                  (nestable)

All integers are little endian. Names in chunk headers and dictionary keys are
stored once in the table of contents and referenced by id (ids start at 1 and are
handed out in order of first use).

Dictionaries (``Dict``) are written as ``uint16 count`` followed, per pair, by
``int32 (keyId << 8 | type)`` and the value: type 0 bool (1 byte), 1 int (int32),
2 real (float32), 3 ascii string (uint16 length + bytes), 4 unicode string
(uint16 length + UTF-16LE).
"""

import struct

DICT_BOOL, DICT_INT, DICT_REAL, DICT_ASCII, DICT_UNICODE = 0, 1, 2, 3, 4


class Dict:
    """An ordered key/value map as the engine's ``Dict`` stores it.

    Values are Python ``bool``/``int``/``float``/``str`` (a ``str`` is written as
    an ASCII string; use :class:`Unicode` for a unicode string).
    """

    def __init__(self, **items):
        self.pairs = []
        for k, v in items.items():
            self.set(k, v)

    def set(self, key, value):
        for i, (k, _) in enumerate(self.pairs):
            if k == key:
                self.pairs[i] = (key, value)
                return self
        self.pairs.append((key, value))
        return self

    def get(self, key, default=None):
        for k, v in self.pairs:
            if k == key:
                return v
        return default

    def __contains__(self, key):
        return any(k == key for k, _ in self.pairs)

    def copy(self):
        d = Dict()
        d.pairs = list(self.pairs)
        return d


class Unicode(str):
    """Marks a string that must be stored as a unicode (UTF-16) Dict value."""


class ChunkWriter:
    """Builds a data chunk stream in memory."""

    def __init__(self):
        self._names = {}      # name -> id
        self._order = []
        self._body = bytearray()
        self._stack = []      # offsets of size placeholders

    # -- symbol table ------------------------------------------------------------
    def _id(self, name):
        if name not in self._names:
            self._names[name] = len(self._order) + 1
            self._order.append(name)
        return self._names[name]

    # -- chunk structure ---------------------------------------------------------
    def open(self, name, version=1):
        self._body += struct.pack("<IH", self._id(name), version)
        self._stack.append(len(self._body))
        self._body += struct.pack("<i", 0xFFFF)

    def close(self):
        pos = self._stack.pop()
        size = len(self._body) - pos - 4
        struct.pack_into("<i", self._body, pos, size)

    class _Scope:
        def __init__(self, writer, name, version):
            self.writer, self.name, self.version = writer, name, version

        def __enter__(self):
            self.writer.open(self.name, self.version)
            return self.writer

        def __exit__(self, *exc):
            self.writer.close()

    def chunk(self, name, version=1):
        """Context manager: ``with w.chunk("Object", 3): ...``"""
        return ChunkWriter._Scope(self, name, version)

    # -- primitives ----------------------------------------------------------------
    def int(self, value):
        self._body += struct.pack("<i", value)

    def uint(self, value):
        self._body += struct.pack("<I", value)

    def real(self, value):
        self._body += struct.pack("<f", value)

    def byte(self, value):
        self._body += struct.pack("<b" if value < 0 else "<B", value)

    def bytes(self, raw):
        self._body += raw

    def ascii(self, text):
        raw = text.encode("ascii")
        self._body += struct.pack("<H", len(raw)) + raw

    def unicode(self, text):
        raw = text.encode("utf-16-le")
        self._body += struct.pack("<H", len(raw) // 2) + raw

    def name_key(self, name):
        self._body += struct.pack("<i", (self._id(name) << 8) | DICT_ASCII)

    def dict(self, d):
        self._body += struct.pack("<H", len(d.pairs))
        for key, value in d.pairs:
            if isinstance(value, bool):
                kind = DICT_BOOL
            elif isinstance(value, int):
                kind = DICT_INT
            elif isinstance(value, float):
                kind = DICT_REAL
            elif isinstance(value, Unicode):
                kind = DICT_UNICODE
            elif isinstance(value, str):
                kind = DICT_ASCII
            else:
                raise TypeError("unsupported dict value %r" % (value,))
            self._body += struct.pack("<i", (self._id(key) << 8) | kind)
            if kind == DICT_BOOL:
                self._body += struct.pack("<B", 1 if value else 0)
            elif kind == DICT_INT:
                self._body += struct.pack("<i", value)
            elif kind == DICT_REAL:
                self._body += struct.pack("<f", value)
            elif kind == DICT_ASCII:
                self.ascii(value)
            else:
                self.unicode(value)

    # -- output ----------------------------------------------------------------------
    def to_bytes(self):
        if self._stack:
            raise ValueError("unclosed chunk")
        toc = bytearray(b"CkMp")
        toc += struct.pack("<i", len(self._order))
        for name in self._order:
            raw = name.encode("ascii")
            toc += struct.pack("<B", len(raw)) + raw + struct.pack("<I", self._names[name])
        return bytes(toc) + bytes(self._body)


class ChunkReader:
    """Reads a data chunk stream; used by the tests to verify written files."""

    def __init__(self, data):
        if data[:4] != b"CkMp":
            raise ValueError("missing table of contents")
        (count,) = struct.unpack_from("<i", data, 4)
        pos = 8
        self.names = {}
        for _ in range(count):
            n = data[pos]
            name = data[pos + 1:pos + 1 + n].decode("ascii")
            (ident,) = struct.unpack_from("<I", data, pos + 1 + n)
            self.names[ident] = name
            pos += 1 + n + 4
        self.data = data
        self.start = pos

    def chunks(self, pos=None, end=None):
        """Yield ``(name, version, data_offset, size)`` for the chunks in a range."""
        pos = self.start if pos is None else pos
        end = len(self.data) if end is None else end
        while pos < end:
            ident, version, size = struct.unpack_from("<IHi", self.data, pos)
            if ident not in self.names:
                raise ValueError("unknown chunk id %d at %d" % (ident, pos))
            if pos + 10 + size > end:
                raise ValueError("chunk %s overruns its parent" % self.names[ident])
            yield self.names[ident], version, pos + 10, size
            pos += 10 + size

    def cursor(self, offset):
        return Cursor(self, offset)


class Cursor:
    """Sequential typed reads from a chunk's data."""

    def __init__(self, reader, offset):
        self.reader = reader
        self.pos = offset

    def int(self):
        (v,) = struct.unpack_from("<i", self.reader.data, self.pos)
        self.pos += 4
        return v

    def real(self):
        (v,) = struct.unpack_from("<f", self.reader.data, self.pos)
        self.pos += 4
        return v

    def byte(self):
        v = self.reader.data[self.pos]
        self.pos += 1
        return v

    def bytes(self, n):
        v = self.reader.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def ascii(self):
        (n,) = struct.unpack_from("<H", self.reader.data, self.pos)
        v = self.reader.data[self.pos + 2:self.pos + 2 + n].decode("ascii")
        self.pos += 2 + n
        return v

    def unicode(self):
        (n,) = struct.unpack_from("<H", self.reader.data, self.pos)
        v = self.reader.data[self.pos + 2:self.pos + 2 + n * 2].decode("utf-16-le")
        self.pos += 2 + n * 2
        return v

    def dict(self):
        (count,) = struct.unpack_from("<H", self.reader.data, self.pos)
        self.pos += 2
        d = Dict()
        for _ in range(count):
            key_and_type = self.int()
            kind = key_and_type & 0xFF
            key = self.reader.names[key_and_type >> 8]
            if kind == DICT_BOOL:
                value = bool(self.byte())
            elif kind == DICT_INT:
                value = self.int()
            elif kind == DICT_REAL:
                value = self.real()
            elif kind == DICT_ASCII:
                value = self.ascii()
            elif kind == DICT_UNICODE:
                value = Unicode(self.unicode())
            else:
                raise ValueError("bad dict type %d" % kind)
            d.pairs.append((key, value))
        return d
