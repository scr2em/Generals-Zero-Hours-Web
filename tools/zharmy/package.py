"""The ``.zharmy`` container: a ZIP archive with ``manifest.json`` and the army's files.

Deterministic output: entries sorted by name, fixed time stamp, fixed attributes, fixed compression level. All
entry names written by this tool are lower case (the format matches names case-insensitively; lower case makes
the content hash independent of how a reader spells the names).

``contentHash`` (see docs/ARMY_PACKAGES.md): SHA-256 over every entry except ``manifest.json``, sorted by the
lower-cased entry name, each fed as ``name`` (lower case, UTF-8), a zero byte, the size as 64-bit little endian,
and the data.
"""

import hashlib
import zlib
import json
import struct
import zipfile

FORMAT = 1
ZIP_TIME = (1980, 1, 1, 0, 0, 0)
STORED_EXT = (".mp3", ".ogg", ".zip", ".jpg", ".png")


class PackageError(ValueError):
    pass


def _data(v):
    """Entry data is bytes or a function returning bytes (large files are read when they are needed)."""
    return v() if callable(v) else v


def content_hash(files):
    """``files``: {entry name: bytes or a function returning bytes}. ``manifest.json`` is skipped.
    Returns ``"sha256:<hex>"``."""
    h = hashlib.sha256()
    items = sorted(((n.lower(), d) for n, d in files.items() if n.lower() != "manifest.json"),
                   key=lambda t: t[0])
    for name, data in items:
        data = _data(data)
        h.update(name.encode("utf-8"))
        h.update(b"\0")
        h.update(struct.pack("<Q", len(data)))
        h.update(struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF))
    return "sha256:" + h.hexdigest()


def content_hash_from_zip(zf):
    """The same hash computed from a ZIP file's directory only (names, sizes, CRC-32s), the way the engine
    computes it; the data is not read."""
    h = hashlib.sha256()
    infos = sorted((i for i in zf.infolist() if i.filename.lower() != "manifest.json"), key=lambda i: i.filename.lower())
    for i in infos:
        h.update(i.filename.lower().encode("utf-8"))
        h.update(b"\0")
        h.update(struct.pack("<Q", i.file_size))
        h.update(struct.pack("<I", i.CRC & 0xFFFFFFFF))
    return "sha256:" + h.hexdigest()


MANIFEST_ORDER = ["format", "id", "tag", "name", "version", "description", "authors", "license", "source",
                  "requires", "factions", "contentHash", "converter"]


def dump_manifest(manifest):
    ordered = {}
    for k in MANIFEST_ORDER:
        if k in manifest:
            ordered[k] = manifest[k]
    for k in manifest:
        if k not in ordered:
            ordered[k] = manifest[k]
    return (json.dumps(ordered, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def write_package(path, manifest, files):
    """Write the package. ``manifest['contentHash']`` is computed here. Returns the manifest as written."""
    manifest = dict(manifest)
    lowered = {}
    for name, data in files.items():
        key = name.replace("\\", "/").lower()
        if key in lowered:
            raise PackageError("two entries with the same name: %s" % key)
        lowered[key] = data
    manifest["contentHash"] = content_hash(lowered)
    entries = [("manifest.json", dump_manifest(manifest))] + sorted(lowered.items())
    with zipfile.ZipFile(path, "w") as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_STORED if name.endswith(STORED_EXT) else zipfile.ZIP_DEFLATED
            z.writestr(info, _data(data), compresslevel=6)
    return manifest


class Package:
    """A package opened for reading. ``entries`` maps lower-cased names to the stored names."""

    def __init__(self, path):
        self.path = path
        try:
            self._zip = zipfile.ZipFile(path)
        except (zipfile.BadZipFile, OSError) as exc:
            raise PackageError("not a readable ZIP archive: %s" % exc)
        self.infos = {}
        self.problems = []
        for info in self._zip.infolist():
            name = info.filename
            if name.endswith("/"):
                continue
            key = name.lower()
            if key in self.infos:
                self.problems.append("duplicate entry name %s" % name)
            self.infos[key] = info
        self._cache = {}

    def names(self):
        return sorted(self.infos)

    def has(self, name):
        return name.lower() in self.infos

    def read(self, name):
        key = name.lower()
        if key not in self._cache:
            self._cache[key] = self._zip.read(self.infos[key])
        return self._cache[key]

    def stored_name(self, name):
        return self.infos[name.lower()].filename

    def manifest(self):
        if not self.has("manifest.json"):
            raise PackageError("manifest.json is missing")
        try:
            return json.loads(self.read("manifest.json").decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise PackageError("manifest.json is not valid UTF-8 JSON: %s" % exc)

    def files(self):
        return {n: self.read(n) for n in self.names()}

    def compute_hash(self):
        return content_hash_from_zip(self._zip)

    def damaged_entry(self):
        """The first entry whose data does not match its CRC-32 (or None)."""
        return self._zip.testzip()

    def close(self):
        self._zip.close()
