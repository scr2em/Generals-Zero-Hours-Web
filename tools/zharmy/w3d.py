"""W3D files: a chunk tree reader/writer and the renaming the army package format needs.

A W3D file is a sequence of chunks ``uint32 type, uint32 size`` followed by ``size & 0x7FFFFFFF`` bytes; when the
top bit of ``size`` is set the data is itself a sequence of chunks. All values are little endian. The layouts of
the chunks that carry names come from ``w3d_file.h`` of the engine's WW3D2 library.

Names in W3D are fixed 16 byte fields (15 characters and a NUL), "full names" are 32 byte fields of the form
``<container>.<object>``. The asset manager keys render objects, hierarchy trees and animations by these names
and loads missing ones from ``Art/W3D/<name>.w3d`` (animations: ``<animation name>.w3d``). To make a model
private to a package every name the file *defines* is replaced by a short generated name, and every reference to
a defined name (hierarchy of an HLOD, sub objects, aggregates, animations, texture names) is rewritten to match.
The object part after the dot of a full name (a mesh name such as ``HOUSECOLOR01`` or ``TREADSL``) is kept: the
engine looks at its first letters to decide how to draw the mesh.
"""

import struct

SUB_FLAG = 0x80000000

# chunk ids
MESH_HEADER3 = 0x1F
TEXTURE_NAME = 0x32
HIERARCHY_HEADER = 0x101
ANIM_HEADER = 0x201
COMPRESSED_ANIM_HEADER = 0x281
MORPH_ANIM_HEADER = 0x2C1
HMODEL_HEADER = 0x301
LODMODEL_HEADER = 0x401
LOD_ENTRY = 0x402
COLLECTION_HEADER = 0x421
COLLECTION_OBJ_NAME = 0x422
EMITTER_HEADER = 0x501
EMITTER_INFO = 0x503
EMITTER_INFOV2 = 0x504
AGGREGATE_HEADER = 0x601
AGGREGATE_INFO = 0x602
TEXTURE_REPLACER_INFO = 0x603
HLOD_HEADER = 0x701
HLOD_SUB_OBJECT = 0x704
BOX = 0x740
NULL_OBJECT = 0x750
DAZZLE_NAME = 0x901
SOUNDROBJ_HEADER = 0xA01

# (offset, length, role); roles: def (defines a name), hier (names a hierarchy), full (full name reference)
FIELDS = {
    HIERARCHY_HEADER: [(4, 16, "def")],
    ANIM_HEADER: [(4, 16, "def"), (20, 16, "hier")],
    COMPRESSED_ANIM_HEADER: [(4, 16, "def"), (20, 16, "hier")],
    MORPH_ANIM_HEADER: [(4, 16, "def"), (20, 16, "hier")],
    HMODEL_HEADER: [(4, 16, "def"), (20, 16, "hier")],
    LODMODEL_HEADER: [(4, 16, "def")],
    LOD_ENTRY: [(0, 32, "full")],
    COLLECTION_HEADER: [(4, 16, "def")],
    EMITTER_HEADER: [(4, 16, "def")],
    AGGREGATE_HEADER: [(4, 16, "def")],
    HLOD_HEADER: [(8, 16, "def"), (24, 16, "hier")],
    HLOD_SUB_OBJECT: [(4, 32, "full")],
    BOX: [(8, 32, "full")],
    NULL_OBJECT: [(16, 32, "full")],
    SOUNDROBJ_HEADER: [(4, 16, "def")],
}
STRING_FULL = (COLLECTION_OBJ_NAME, DAZZLE_NAME)


class W3dError(ValueError):
    pass


class Chunk:
    __slots__ = ("type", "data", "children")

    def __init__(self, ctype, data=b"", children=None):
        self.type = ctype
        self.data = data
        self.children = children          # list of Chunk for container chunks, else None


def parse(data):
    """Parse a whole W3D file into a list of top level chunks."""
    chunks, pos = _parse_range(data, 0, len(data))
    if pos != len(data):
        raise W3dError("trailing bytes after the last chunk")
    return chunks


def _parse_range(data, pos, end):
    out = []
    while pos < end:
        if pos + 8 > end:
            raise W3dError("truncated chunk header at %d" % pos)
        ctype, size = struct.unpack_from("<II", data, pos)
        sub = bool(size & SUB_FLAG)
        size &= ~SUB_FLAG
        start = pos + 8
        if start + size > end:
            raise W3dError("chunk 0x%X at %d overruns its parent" % (ctype, pos))
        if sub:
            kids, p = _parse_range(data, start, start + size)
            out.append(Chunk(ctype, b"", kids))
        else:
            out.append(Chunk(ctype, data[start:start + size], None))
        pos = start + size
    return out, pos


def serialize(chunks):
    out = bytearray()
    for c in chunks:
        if c.children is not None:
            body = serialize(c.children)
            out += struct.pack("<II", c.type, len(body) | SUB_FLAG) + body
        else:
            out += struct.pack("<II", c.type, len(c.data)) + c.data
    return bytes(out)


def walk(chunks):
    for c in chunks:
        yield c
        if c.children:
            yield from walk(c.children)


# ---- fixed fields --------------------------------------------------------------------------------------------
def _get(data, off, length):
    raw = data[off:off + length]
    nul = raw.find(b"\0")
    if nul >= 0:
        raw = raw[:nul]
    return raw.decode("latin-1")


def _put(data, off, length, text):
    raw = text.encode("latin-1")
    if len(raw) >= length:
        raise W3dError("name %r does not fit a %d byte field" % (text, length))
    field = raw + b"\0" * (length - len(raw))
    return data[:off] + field + data[off + length:]


def split_full(name):
    """``container.object`` -> (container, object) ; a name without a dot has no container."""
    if "." in name:
        a, b = name.split(".", 1)
        return a, b
    return None, name


# ---- queries ---------------------------------------------------------------------------------------------------
class Names:
    """What a W3D file defines and refers to (all names lower case)."""

    def __init__(self):
        self.defined = set()       # prototype / hierarchy / animation names defined in the file
        self.hierarchies = set()   # hierarchy names that HLODs, models and animations refer to
        self.refs = set()          # container names used in full names (sub objects, aggregates, ...)
        self.textures = []         # texture names as written (in file order, duplicates removed)

    def external(self):
        """Names that are referred to but not defined here."""
        return (self.hierarchies | self.refs) - self.defined


def scan(chunks):
    n = Names()
    seen_tex = set()
    for c in walk(chunks):
        if c.children is not None:
            continue
        d = c.data
        if c.type == MESH_HEADER3:
            mesh = _get(d, 8, 16)
            cont = _get(d, 24, 16)
            n.defined.add((cont or mesh).lower())
        elif c.type == TEXTURE_NAME:
            t = _get(d, 0, len(d))
            if t and t.lower() not in seen_tex:
                seen_tex.add(t.lower())
                n.textures.append(t)
        elif c.type in (EMITTER_INFO, EMITTER_INFOV2):
            t = _get(d, 0, min(260, len(d)))
            if t and t.lower() not in seen_tex:
                seen_tex.add(t.lower())
                n.textures.append(t)
        elif c.type in STRING_FULL:
            cont, _ = split_full(_get(d, 0, len(d)))
            if cont:
                n.refs.add(cont.lower())
        elif c.type == AGGREGATE_INFO:
            base_c, _ = split_full(_get(d, 0, 32))
            if base_c:
                n.refs.add(base_c.lower())
            if len(d) >= 36:
                (count,) = struct.unpack_from("<I", d, 32)
                for i in range(count):
                    off = 36 + i * 64
                    if off + 32 > len(d):
                        break
                    sc, _ = split_full(_get(d, off, 32))
                    if sc:
                        n.refs.add(sc.lower())
        elif c.type in FIELDS:
            for off, length, role in FIELDS[c.type]:
                if off + length > len(d):
                    continue
                v = _get(d, off, length)
                if not v:
                    continue
                if role == "def":
                    n.defined.add(v.lower())
                elif role == "hier":
                    n.hierarchies.add(v.lower())
                else:
                    cont, _ = split_full(v)
                    if cont:
                        n.refs.add(cont.lower())
    return n


# ---- renaming --------------------------------------------------------------------------------------------------
def rename(chunks, names, textures):
    """Rewrite the chunks in place.

    ``names``: {lower-case old name: new name} for every name defined by the package's W3D files.
    ``textures``: {lower-case old texture name (with extension): new texture name} (a missing key keeps the name).
    Returns the number of fields changed.
    """
    changed = 0

    def new_full(full):
        cont, obj = split_full(full)
        if cont is None:
            return full
        mapped = names.get(cont.lower())
        return full if mapped is None else mapped + "." + obj

    for c in walk(chunks):
        if c.children is not None:
            continue
        d = c.data
        if c.type == MESH_HEADER3:
            mesh = _get(d, 8, 16)
            cont = _get(d, 24, 16)
            if cont:
                new = names.get(cont.lower())
                if new:
                    d = _put(d, 24, 16, new)
            else:
                new = names.get(mesh.lower())
                if new:
                    d = _put(d, 8, 16, new)
        elif c.type == TEXTURE_NAME:
            t = _get(d, 0, len(d))
            new = textures.get(t.lower())
            if new:
                d = new.encode("latin-1") + b"\0"
        elif c.type in (EMITTER_INFO, EMITTER_INFOV2):
            t = _get(d, 0, min(260, len(d)))
            new = textures.get(t.lower())
            if new:
                if len(new) >= 260:
                    raise W3dError("texture name too long")
                d = new.encode("latin-1") + b"\0" * (260 - len(new)) + d[260:]
        elif c.type in STRING_FULL:
            old = _get(d, 0, len(d))
            new = new_full(old)
            if new != old:
                d = new.encode("latin-1") + b"\0"
        elif c.type == AGGREGATE_INFO:
            old = _get(d, 0, 32)
            new = new_full(old)
            if new != old:
                d = _put(d, 0, 32, new)
            if len(d) >= 36:
                (count,) = struct.unpack_from("<I", d, 32)
                for i in range(count):
                    off = 36 + i * 64
                    if off + 32 > len(d):
                        break
                    o = _get(d, off, 32)
                    nw = new_full(o)
                    if nw != o:
                        d = _put(d, off, 32, nw)
        elif c.type in FIELDS:
            for off, length, role in FIELDS[c.type]:
                if off + length > len(d):
                    continue
                v = _get(d, off, length)
                if not v:
                    continue
                if role in ("def", "hier"):
                    new = names.get(v.lower())
                else:
                    nv = new_full(v)
                    new = None if nv == v else nv
                if new and new != v:
                    d = _put(d, off, length, new)
        if d is not c.data:
            if d != c.data:
                changed += 1
            c.data = d
    return changed


def rename_bytes(data, names, textures):
    chunks = parse(data)
    rename(chunks, names, textures)
    return serialize(chunks)
