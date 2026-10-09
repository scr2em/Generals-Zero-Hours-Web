"""A layered, case-insensitive virtual file system over BIG archives and loose folders.

Rules, taken from the engine (``FileSystem::openFile``, ``ArchiveFileSystem::loadIntoDirectoryTree``,
``ArchiveFileSystem::loadMods``):

* a loose file always wins over any archive;
* archives found by one directory scan are loaded in case-insensitive alphabetical order of their paths;
  game archives (``overwrite`` false) keep the first archive that holds a file, mod archives
  (``overwrite`` true, ``-mod`` / ``-modBIG``) let the archive loaded later replace earlier ones;
* the layers given to this class are searched from the last added to the first added, so the
  ruleset is added first and mods after it.

Paths are normalised to lower case with ``/`` separators.
"""

import fnmatch
import os

from .bigfile import BigArchive, BigError

_SOURCES = {}      # (absolute path, size, mtime) -> Source: archives are opened (directory read) once per run


def norm(path):
    return path.replace("\\", "/").strip("/").lower()


class Source:
    """One place that can deliver files: a loose folder or a BIG archive."""

    def __init__(self, label):
        self.label = label
        self.index = {}      # normalised path -> handle (opaque)

    def read(self, handle):
        raise NotImplementedError


class FolderSource(Source):
    def __init__(self, folder, exclude=None):
        super().__init__(folder)
        self.folder = folder
        for dirpath, _dirs, files in os.walk(folder):
            for name in files:
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, folder)
                key = norm(rel)
                if key.endswith(".big"):
                    continue
                if exclude is not None and exclude(key):
                    continue
                self.index.setdefault(key, full)

    def read(self, handle):
        with open(handle, "rb") as f:
            return f.read()


class BigSource(Source):
    def __init__(self, path):
        super().__init__(path)
        self.archive = BigArchive(path)
        for name in self.archive.names():
            self.index[norm(name)] = name

    def read(self, handle):
        return self.archive.read(handle)


class MemorySource(Source):
    """Files held in memory (tests and generated data)."""

    def __init__(self, files, label="memory"):
        super().__init__(label)
        self.files = {}
        for k, v in files.items():
            key = norm(k)
            self.files[key] = v
            self.index[key] = key

    def read(self, handle):
        return self.files[handle]


def cached_source(kind, path):
    st = os.stat(path)
    key = (os.path.abspath(path), st.st_size, int(st.st_mtime))
    src = _SOURCES.get(key)
    if src is None:
        src = BigSource(path)
        _SOURCES[key] = src
    return src


def glob_matcher(patterns):
    """A predicate for paths relative to a game folder: true when a pattern matches the whole relative path or the
    file name (case-insensitive, ``/`` separators, ``*`` ``?`` ``[]``)."""
    pats = [p.replace("\\", "/").lower() for p in patterns]

    def match(rel):
        rel = rel.lower()
        name = rel.rsplit("/", 1)[-1]
        return any(fnmatch.fnmatchcase(rel, p) or fnmatch.fnmatchcase(name, p) for p in pats)
    return match


class Vfs:
    def __init__(self):
        self.layers = []     # lowest priority first
        self.problems = []   # archives that could not be read: (path, message)

    # -- building -----------------------------------------------------------------------------------
    def add_source(self, source):
        self.layers.append(source)
        return source

    def add_big(self, path):
        try:
            return self.add_source(cached_source("big", path))
        except (BigError, OSError) as exc:
            self.problems.append((path, str(exc)))
            return None

    def add_folder(self, folder, exclude=None):
        return self.add_source(FolderSource(folder, exclude))

    def add_memory(self, files, label="memory"):
        return self.add_source(MemorySource(files, label))

    def add_tree(self, path, overwrite=True, exclude=None):
        """Add a mod or game location: a ``.big`` file, or a folder (loose files plus the ``.big`` files in it).

        Loose files win over the archives of the same folder. ``overwrite`` follows the engine: for mod
        archives the archive loaded last (alphabetically) wins, for game archives the one loaded first.
        """
        if os.path.isfile(path):
            if not path.lower().endswith(".big"):
                raise ValueError("%s is neither a folder nor a .big file" % path)
            self.add_big(path)
            return
        if not os.path.isdir(path):
            raise FileNotFoundError(path)
        bigs = []
        for dirpath, _dirs, files in os.walk(path):
            for name in files:
                if name.lower().endswith(".big"):
                    full = os.path.join(dirpath, name)
                    rel = norm(os.path.relpath(full, path))
                    if rel.endswith("data/ini/inizh.big"):
                        continue        # the engine skips this duplicate of INIZH.big (StdBIGFileSystem.cpp)
                    if exclude is not None and exclude(rel):
                        continue
                    bigs.append(full)
        bigs.sort(key=lambda p: os.path.relpath(p, path).lower())
        if not overwrite:
            bigs.reverse()              # first loaded must end up with the highest priority
        for big in bigs:
            self.add_big(big)
        self.add_folder(path, exclude)

    # -- queries --------------------------------------------------------------------------------------
    def _find(self, path):
        key = norm(path)
        for layer in reversed(self.layers):
            handle = layer.index.get(key)
            if handle is not None:
                return layer, handle
        return None

    def exists(self, path):
        return self._find(path) is not None

    def read(self, path):
        found = self._find(path)
        if found is None:
            raise FileNotFoundError(path)
        return found[0].read(found[1])

    def same_file(self, other, path):
        """True when ``path`` exists in both file systems with identical contents."""
        a = self._find(path)
        b = other._find(path)
        if a is None or b is None:
            return False
        if a[0] is b[0] and a[1] == b[1]:
            return True
        return a[0].read(a[1]) == b[0].read(b[1])

    def source_of(self, path):
        found = self._find(path)
        return found[0].label if found else None

    def listdir(self, directory):
        """Case-insensitively listed immediate files below ``directory``: returns {normalised path: True}."""
        prefix = norm(directory)
        prefix = prefix + "/" if prefix else ""
        seen = {}
        for layer in self.layers:
            for key in layer.index:
                if key.startswith(prefix) and "/" not in key[len(prefix):]:
                    seen[key] = True
        return sorted(seen)

    def walk(self, directory):
        """Every file below ``directory`` (recursively), sorted by normalised path."""
        prefix = norm(directory)
        prefix = prefix + "/" if prefix else ""
        seen = set()
        for layer in self.layers:
            for key in layer.index:
                if key.startswith(prefix):
                    seen.add(key)
        return sorted(seen)

    def all_paths(self):
        seen = set()
        for layer in self.layers:
            seen.update(layer.index)
        return sorted(seen)
