"""Game folder layout: which archives belong to the retail game, how the engine layers several installs, and which
archives a mod added.

What the engine does (``StdBIGFileSystem::init`` / ``Win32BIGFileSystem::init``, ``FileSystem::openFile``):

* the Zero Hour folder (the working directory) is searched first: loose files, then every ``*.big`` below it
  (``getFileListInDirectory(..., TRUE)``, case-insensitive alphabetical order of the full path), the first archive
  that holds a file wins;
* then the base Generals install (the path in the registry, ``GetStringFromGeneralsRegistry``) is loaded the same
  way, *after* all Zero Hour archives, so its archives only supply what Zero Hour does not have. Only archives are
  read there, loose files of the Generals folder are not visible to the engine.

A folder that holds both installs as sub folders (``command and conquer generals zero hour/`` and
``command and conquer generals/``) is therefore layered Zero Hour first, Generals second. ``detect_layout`` finds
that arrangement; ``add_game_tree`` builds the layers of one folder under these rules.

A mod installed into the game folder adds archives with names of its own (``!00mypatch.big``). ``is_retail_archive``
knows the file names of the retail archives, so ``--mod-archives auto`` can take every other archive as the mod's.
"""

import os
import re

from .vfs import glob_matcher, norm

LANGUAGES = ("english", "german", "french", "spanish", "italian", "korean", "chinese", "brazilian", "polish",
             "russian", "swedish", "dutch", "japanese", "portuguese", "czech", "turkish", "hungarian", "finnish",
             "norwegian", "danish", "greek", "arabic", "hebrew", "ukrainian", "thai", "slovak", "romanian",
             "bulgarian", "croatian", "serbian", "lithuanian", "latvian", "estonian", "catalan", "vietnamese",
             "indonesian", "malay")
_LANG = "(?:%s)" % "|".join(LANGUAGES)

# stems (lower case, without ".big" and without the "zh" of Zero Hour) of the retail archives
_BASE = "(?:ini|w3d|textures|terrain|audio|speech|window|shaders|maps|music|gensec|patch)"
# INIZH, W3DZH, W3DEnglishZH, AudioGermanZH, EnglishZH, Music, Patch ...
_RETAIL = re.compile(r"^(?:%s(?:%s)?|%s)(zh)?$" % (_BASE, _LANG, _LANG))

# the explicit list from public knowledge of the retail releases (a subset of what the pattern accepts, kept so the
# intent is visible and testable)
RETAIL_ZERO_HOUR = ("INIZH.big", "W3DZH.big", "W3DEnglishZH.big", "TexturesZH.big", "TerrainZH.big", "AudioZH.big",
                    "AudioEnglishZH.big", "EnglishZH.big", "SpeechZH.big", "SpeechEnglishZH.big", "WindowZH.big",
                    "ShadersZH.big", "MapsZH.big", "MusicZH.big", "Music.big", "GensecZH.big", "PatchZH.big")
RETAIL_GENERALS = ("INI.big", "W3D.big", "Textures.big", "Terrain.big", "Audio.big", "AudioEnglish.big",
                   "English.big", "Speech.big", "SpeechEnglish.big", "Window.big", "Shaders.big", "Maps.big",
                   "Music.big", "Gensec.big", "Patch.big")


def is_retail_archive(name):
    """True when the file name (no folder) is one of the retail Zero Hour / Generals archives or a language
    variant of one (``AudioGermanZH.big``)."""
    base = name.replace("\\", "/").rsplit("/", 1)[-1].lower()
    if not base.endswith(".big"):
        return False
    return bool(_RETAIL.match(base[:-4]))


def is_zero_hour_archive(name):
    base = name.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return base.endswith("zh.big") and is_retail_archive(base)


# ---- install detection --------------------------------------------------------------------------------------------
class Install:
    def __init__(self, path, rel, kind):
        self.path = path
        self.rel = rel                 # relative to the folder that was given ("" for the folder itself)
        self.kind = kind               # zerohour | generals

    def describe(self):
        return "%s (%s)" % (self.rel or ".", "Zero Hour" if self.kind == "zerohour" else "Generals")


class Layout:
    def __init__(self, folder, installs, multi):
        self.folder = folder
        self.installs = installs       # in engine order: Zero Hour first, then Generals
        self.multi = multi

    def lines(self):
        if not self.multi:
            return []
        out = ["Folder layout: %s holds %d game installs as sub folders; the engine loads them in this order "
               "(an archive found earlier wins):" % (self.folder, len(self.installs))]
        for i, inst in enumerate(self.installs, 1):
            note = "loose files and archives" if inst.kind == "zerohour" else "archives only (the engine reads no " \
                                                                              "loose files from the Generals folder)"
            out.append("  %d. %s: %s" % (i, inst.describe(), note))
        return out


def _top_bigs(path):
    try:
        return [n for n in os.listdir(path) if n.lower().endswith(".big") and os.path.isfile(os.path.join(path, n))]
    except OSError:
        return []


def _install_kind(path):
    """'zerohour' / 'generals' when the folder directly holds retail archives, else None."""
    retail = [n for n in _top_bigs(path) if is_retail_archive(n)]
    if not retail:
        return None
    return "zerohour" if any(is_zero_hour_archive(n) for n in retail) else "generals"


def detect_layout(folder):
    """A folder that has retail archives itself is one install. A folder without, but with sub folders that each look
    like an install, is a game folder with several installs (Zero Hour ordered before Generals)."""
    if not os.path.isdir(folder):
        return Layout(folder, [], False)
    own = _install_kind(folder)
    if own is not None:
        return Layout(folder, [Install(folder, "", own)], False)
    found = []
    for name in sorted(os.listdir(folder), key=str.lower):
        sub = os.path.join(folder, name)
        if os.path.isdir(sub):
            kind = _install_kind(sub)
            if kind is not None:
                found.append(Install(sub, name, kind))
    if not found:
        return Layout(folder, [], False)
    found.sort(key=lambda i: (0 if i.kind == "zerohour" else 1, i.rel.lower()))
    return Layout(folder, found, True)


def add_game_tree(vfs, folder, overwrite=False, exclude=None, loose=True):
    """Add ``folder`` to ``vfs`` the way the engine layers a game folder. Returns the Layout.

    ``exclude`` is a predicate on the path relative to ``folder`` (for archives and loose files);
    ``loose=False`` adds the archives only.
    """
    lay = detect_layout(folder)
    if not lay.multi or overwrite:
        vfs.add_tree(folder, overwrite=overwrite, exclude=exclude, loose=loose)
        return lay
    # later layers win: add the last-loaded install first. Only the Zero Hour (working directory) install has
    # loose files that the engine sees.
    for inst in reversed(lay.installs):
        prefix = norm(inst.rel) + "/" if inst.rel else ""
        sub = None if exclude is None else (lambda rel, p=prefix: exclude(p + rel))
        vfs.add_tree(inst.path, overwrite=False, exclude=sub, loose=loose and (inst is lay.installs[0]))
    return lay


# ---- listing the archives -----------------------------------------------------------------------------------------
class Archive:
    def __init__(self, rel, full, install):
        self.rel = rel                 # relative to the folder that was given, '/' separated, original case
        self.full = full
        self.install = install         # Install or None

    @property
    def name(self):
        return self.rel.rsplit("/", 1)[-1]

    @property
    def retail(self):
        return is_retail_archive(self.name)


def list_archives(folder):
    """The ``.big`` files below ``folder`` in engine load order (the first one wins). Skips Data/INI/INIZH.big
    like the engine does."""
    lay = detect_layout(folder)
    roots = [(i.path, i) for i in lay.installs] if lay.multi else [(folder, lay.installs[0] if lay.installs else None)]
    out = []
    for root, inst in roots:
        found = []
        for dirpath, _d, files in os.walk(root):
            for n in files:
                if n.lower().endswith(".big"):
                    full = os.path.join(dirpath, n)
                    if norm(os.path.relpath(full, root)).endswith("data/ini/inizh.big"):
                        continue
                    found.append(full)
        found.sort(key=lambda p: os.path.relpath(p, root).lower())
        for full in found:
            out.append(Archive(os.path.relpath(full, folder).replace("\\", "/"), full, inst))
    return out


def list_loose(folder, limit=None):
    """Relative paths of all non-archive files below ``folder``."""
    out = []
    for dirpath, _d, files in os.walk(folder):
        for n in files:
            if not n.lower().endswith(".big"):
                out.append(os.path.relpath(os.path.join(dirpath, n), folder).replace("\\", "/"))
    return out


# ---- the mod's archives -----------------------------------------------------------------------------------------
class SelectionError(Exception):
    pass


class ModSelection:
    """Which files of the game folder(s) are the mod's (and so not part of the ruleset).

    ``patterns`` as given (``None`` = not given); ``auto`` means non-retail archives; ``picked`` lists the archives
    (relative paths per folder) that end up as the mod's; ``notes`` are lines for the output.
    """

    def __init__(self):
        self.patterns = None
        self.auto = False
        self.globs = []
        self.picked = []               # [(folder, archive rel)]
        self.matched_loose = []
        self.notes = []
        self.layouts = []
        self.explicit = False
        self._glob = None
        self.loose_mod = False         # loose files of the game folder belong to the mod (unless identical to retail)
        self.loose_files = []          # [(folder, rel)] loose files that count as the mod's
        self.loose_identical = 0       # loose files left in the ruleset because they equal the retail archive copy
        self.identical = lambda rel: False
        self.install_prefixes = []

    @property
    def active(self):
        return bool(self.auto or self.globs)

    def archive_match(self, rel):
        """True for an archive (path relative to the folder) that is the mod's."""
        if self._glob is None:
            self._glob = glob_matcher(self.globs) if self.globs else (lambda r: False)
        if self._glob(rel):
            return True
        return bool(self.auto and not is_retail_archive(rel))

    def predicate(self):
        """exclude(rel) for ``Vfs.add_tree``: true for the files that are the mod's."""
        if not self.active:
            return None
        if self._glob is None:
            self._glob = glob_matcher(self.globs) if self.globs else (lambda r: False)
        glob = self._glob

        def match(rel):
            if rel.lower().endswith(".big"):
                return self.archive_match(rel)
            if glob(rel):
                return True
            return self.loose_mod and self.relevant(rel) and not self.identical(rel)
        return match

    def relevant(self, rel):
        """Only loose files below Data/ and Art/ can be game data; executables, movies and the like do not matter."""
        key = norm(rel)
        for pre in self.install_prefixes:
            if pre and key.startswith(pre):
                key = key[len(pre):]
                break
        return key.startswith("data/") or key.startswith("art/")


def _archive_table(folder):
    rows = []
    for a in list_archives(folder):
        try:
            size = os.path.getsize(a.full) / 1048576.0
        except OSError:
            size = 0.0
        rows.append("  %-60s %9.1f MB  %s" % (a.rel, size, "retail" if a.retail else "NOT a retail name"))
    return "\n".join(rows) if rows else "  (no .big files)"


def resolve_mod_archives(base_paths, patterns, same_folder, loose=None):
    """Turn ``--mod-archives`` into a ModSelection and check it.

    * ``patterns`` None: ``auto`` when the mod is installed in the game folder (``same_folder``), else nothing;
      an empty list means "none" on purpose;
    * ``auto`` picks archives whose file name is not a retail archive name;
    * a glob that matches no file is an error that lists the archives;
    * installed mod (``same_folder``) with nothing selected: error (everything would look like the ruleset);
    * ``loose``: ``"mod"`` (the default for an installed mod) counts the loose files of the game folder as the mod's,
      except those identical to the copy in a retail archive; ``"ruleset"`` leaves them in the ruleset.
    """
    sel = ModSelection()
    sel.patterns = None if patterns is None else list(patterns)
    if patterns is None:
        pats = ["auto"] if (same_folder and base_paths) else []
        sel.explicit = False
    else:
        pats = list(patterns)
        sel.explicit = True
    sel.auto = any(p.lower() == "auto" for p in pats)
    sel.globs = [p for p in pats if p.lower() != "auto"]
    if (sel.auto or sel.globs) and not base_paths:
        raise SelectionError("--mod-archives needs --base: it names the archives of the mod inside the game folder "
                             "that --base points to")
    folders = [p for p in base_paths if os.path.isdir(p)]
    for folder in folders:
        lay = detect_layout(folder)
        sel.layouts.append(lay)
        sel.notes.extend(lay.lines())
    if not (sel.auto or sel.globs):
        if same_folder and sel.explicit and base_paths:
            sel.notes.append("--mod-archives names no archive: the whole folder is the ruleset (everything looks "
                             "like retail data)")
        return sel
    # every pattern must match something
    for pat in sel.globs:
        match = glob_matcher([pat])
        hits = []
        for folder in folders:
            hits.extend((folder, a.rel) for a in list_archives(folder) if match(a.rel))
        if not hits:
            for folder in folders:
                hits.extend((folder, r) for r in list_loose(folder) if match(r))
        if not hits:
            where = "\n".join("Archives in %s:\n%s" % (f, _archive_table(f)) for f in folders) or "(no folder)"
            raise SelectionError("--mod-archives pattern %r matches no archive or file in %s.\n%s\n"
                                 "Patterns are matched case-insensitively against the file name or the path "
                                 "relative to the folder (* ? [] allowed). Use 'auto' to take every archive whose "
                                 "name is not a retail archive name." % (pat, ", ".join(folders), where))
    if loose not in (None, "mod", "ruleset"):
        raise SelectionError("--loose must be 'mod' or 'ruleset'")
    total_archives = sum(len(list_archives(f)) for f in folders)
    sel.loose_mod = (loose == "mod") or (loose is None and same_folder and total_archives > 0)
    if sel.loose_mod and loose == "mod" and not same_folder:
        sel.loose_mod = True
    prepare_loose(sel, folders)
    pred = sel.predicate()
    for folder in folders:
        for a in list_archives(folder):
            if sel.archive_match(a.rel):
                sel.picked.append((folder, a.rel))
        for r in list_loose(folder):
            if sel.globs and glob_matcher(sel.globs)(r):
                sel.matched_loose.append((folder, r))
    if sel.auto and not sel.globs:
        total = sum(len(list_archives(f)) for f in folders)
        if not sel.picked and not sel.loose_files and total:
            raise SelectionError(
                "--mod-archives auto found no archive that is not a retail one in %s: every one of the %d archives "
                "has a retail name, so the folder looks like the unmodified game and everything would look like "
                "retail data. Name the mod's archives with --mod-archives '<glob>' ...; the archives are:\n%s\n"
                "(loose mod files can be named too: --mod-archives 'Data/INI/*')"
                % (", ".join(folders), total, "\n".join(_archive_table(f) for f in folders)))
    if same_folder and not sel.picked and not sel.matched_loose and not sel.loose_files:
        total = sum(len(list_archives(f)) for f in folders)
        if total:
            raise SelectionError("the mod folder and --base are the same folder but --mod-archives selects nothing: "
                                 "every definition would look like retail data.\n" +
                                 "\n".join(_archive_table(f) for f in folders))
    how = "auto = archives without a retail file name" if sel.auto else "by pattern"
    if sel.auto and sel.globs:
        how = "auto and by pattern"
    if sel.picked or sel.matched_loose:
        sel.notes.append("Mod archives (%s): %d selected, they are not part of the ruleset:" % (how, len(sel.picked)))
        for folder, rel in sel.picked:
            sel.notes.append("  " + rel)
        if sel.matched_loose:
            sel.notes.append("  and %d loose file(s) matching the patterns" % len(sel.matched_loose))
    elif sel.auto:
        sel.notes.append("--mod-archives auto: the folder has no archives; it is used as the ruleset as it is")
    sel.notes.extend(loose_lines(sel))
    return sel


def _first_install_rel(folder):
    lay = detect_layout(folder)
    if lay.multi and lay.installs:
        return norm(lay.installs[0].rel) + "/"
    return ""


def prepare_loose(sel, folders):
    """Work out which loose files of the game folders are the mod's: all of them, except those that are byte-identical
    to the copy in a retail archive (a clean install may carry loose files too). Fills ``sel.loose_files``,
    ``sel.loose_identical`` and ``sel.identical``."""
    sel.loose_files, sel.loose_identical = [], 0
    sel.install_prefixes = [_first_install_rel(f) for f in folders]
    if not sel.loose_mod:
        return
    from .vfs import Vfs
    retail = Vfs()
    for f in folders:
        add_game_tree(retail, f, overwrite=False, exclude=sel.archive_match, loose=False)

    def identical(rel):
        key = norm(rel)
        for f in folders:
            pre = _first_install_rel(f)
            if pre and key.startswith(pre):
                key = key[len(pre):]
            if retail.exists(key):
                full = os.path.join(f, rel)
                try:
                    with open(full, "rb") as handle:
                        return handle.read() == retail.read(key)
                except OSError:
                    return False
        return False
    sel.identical = identical
    for f in folders:
        pre = _first_install_rel(f)
        lay = detect_layout(f)
        for rel in list_loose(f):
            if lay.multi and pre and not norm(rel).startswith(pre):
                continue                  # loose files of the Generals install: the engine does not see them
            if not sel.relevant(rel):
                continue
            if identical(rel):
                sel.loose_identical += 1
            else:
                sel.loose_files.append((f, rel))


def loose_lines(sel):
    if not sel.loose_mod:
        return []
    if not sel.loose_files and not sel.loose_identical:
        return []
    tops = {}
    for _f, rel in sel.loose_files:
        parts = rel.lower().split("/")
        key = "/".join(parts[:3] if parts[0] == "data" and len(parts) > 3 else parts[:2] if len(parts) > 2 else parts[:1])
        tops[key] = tops.get(key, 0) + 1
    top = ", ".join("%s (%d)" % kv for kv in sorted(tops.items(), key=lambda kv: -kv[1])[:6])
    out = ["Loose files treated as the mod's (not part of the ruleset): %d%s%s" % (
        len(sel.loose_files), "; mostly " + top if top else "",
        "; %d more are identical to the retail archive copy and stay ruleset" % sel.loose_identical
        if sel.loose_identical else "")]
    out.append("  (--loose ruleset treats loose files as retail data instead)")
    return out
