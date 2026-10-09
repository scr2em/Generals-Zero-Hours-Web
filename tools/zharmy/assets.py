"""Collecting the model, texture and sound files an army needs, and renaming them.

Names given to the package's files:

* W3D files, and every name defined inside them (render objects, hierarchies, animations, containers, emitters):
  ``<TAG><counter in base 36>`` (e.g. ``CNUK00A``): short, because W3D stores such names in 16 byte fields.
  The map is returned so it can go into the report.
* textures used by W3D files: the same scheme (the texture name is a W3D name field in older tools);
  textures only named in INI files: ``<TAG>_<original name>``;
* sounds, speech and music: ``<TAG>_<original name>``.

A file that the ruleset already has with identical contents stays a reference (when the package may rely on the
ruleset). Files found nowhere are reported and their references left as they were.
"""

import functools
import os
from collections import deque

from . import search, w3d

TEXTURE_EXTS = (".dds", ".tga")


def base36(n, width=3):
    digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    out = ""
    while n:
        n, r = divmod(n, 36)
        out = digits[r] + out
    return (out or "0").rjust(width, "0")


class Namer:
    def __init__(self, tag):
        self.tag = tag
        self.counter = 0
        self.used = set()

    def next(self, prefix=""):
        while True:
            self.counter += 1
            name = prefix + self.tag + base36(self.counter)
            if name.lower() not in self.used:
                self.used.add(name.lower())
                return name


def split_ext(name):
    base, ext = os.path.splitext(name)
    return base, ext


class W3dEntry:
    def __init__(self, status, path=None, chunks=None, scan=None):
        self.status = status          # copy | base | missing | invalid
        self.path = path
        self.chunks = chunks
        self.scan = scan


class AssetPlanner:
    def __init__(self, mod, base, tag, use_base, language="english", zhc_names=True, warn=None, info=None):
        self.mod, self.base, self.tag = mod, base, tag
        self.use_base = use_base and base is not None
        self.language = (language or "english").lower()
        self.zhc = zhc_names
        self.warn = warn or (lambda m: None)
        self.info = info or (lambda m: None)
        self.namer = Namer(tag)
        self.needs = {k: {} for k in ("Model", "Anim", "Texture", "AudioFile", "TrackFile", "SpeechFile")}
        self.w3d = {}
        self.names = {}                 # W3D names (lower) -> new
        self.tex = {}                   # texture stem (lower) -> new stem
        self.audio = {"AudioFile": {}, "TrackFile": {}, "SpeechFile": {}}   # lower token -> new token
        self.files = {}                 # package path -> bytes
        self.report = {"copied": [], "reference": [], "missing": [], "invalid": []}
        self._tex_status = {}
        self._zhc_lost = []
        self.settings = search.audio_settings(mod)
        self._lang_index = None
        self.duplicates = []            # (name, [files]) W3D names defined by more than one copied file

    # ---- input ------------------------------------------------------------------------------------------
    def add(self, kind, token):
        if not token or token.lower() in ("none", "nosound"):
            return
        self.needs[kind].setdefault(token.lower(), token)

    # ---- helpers ----------------------------------------------------------------------------------------
    def _same_as_base(self, path):
        return self.use_base and self.mod.same_file(self.base, path)

    def _record(self, kind, status, old, new=None, path=None, hint=None):
        rec = {"kind": kind, "name": old, "new": new, "path": path}
        if hint:
            rec["hint"] = hint
        self.report[status].append(rec)

    def _hint(self, kind, token):
        """Why a file may be missing although it exists somewhere the engine does not look."""
        if self._lang_index is None:
            self._lang_index = search.language_index(self.mod)
        other = search.other_language_hint(self._lang_index, kind, token)
        if other:
            return "exists as %s, but the engine reads that folder only for its language (searched: %s)" % (
                other, self.language)
        if kind == "Texture":
            alt = search.same_stem_other_extension(self.mod, token)
            if alt:
                return "%s exists, but the engine does not substitute the other texture extension" % alt
        return None

    # ---- W3D ----------------------------------------------------------------------------------------------
    def _w3d_queue(self):
        queue = deque()
        for tok in sorted(self.needs["Model"]):
            queue.append((search.w3d_stem("Model", tok), True))
        for tok in sorted(self.needs["Anim"]):
            hier, _, anim = tok.partition(".")
            if anim:
                queue.append((anim, True))
                queue.append((hier, False))
            else:
                queue.append((tok, True))
        while queue:
            stem, wanted = queue.popleft()
            stem = stem.lower()
            if stem in self.w3d:
                continue
            path = search.first_existing(self.mod, search.w3d_paths(stem, self.language))
            if path is None:
                self.w3d[stem] = W3dEntry("missing", "art/w3d/%s.w3d" % stem)
                continue
            if self._same_as_base(path):
                self.w3d[stem] = W3dEntry("base", path)
                continue
            data = self.mod.read(path)
            try:
                chunks = w3d.parse(data)
                sc = w3d.scan(chunks)
            except w3d.W3dError as exc:
                self.w3d[stem] = W3dEntry("invalid", path)
                self.warn("W3D file %s is not valid (%s); references to it are left unchanged" % (path, exc))
                continue
            self.w3d[stem] = W3dEntry("copy", path, chunks, sc)
            for ext in sorted(sc.external()):
                queue.append((ext, False))

    def _plan_w3d(self):
        self._w3d_queue()
        copies = sorted(s for s, e in self.w3d.items() if e.status == "copy")
        defined_somewhere = set()
        for s in copies:
            defined_somewhere |= self.w3d[s].scan.defined
        # names first by file stem so the file name and the name inside agree
        for s in copies:
            self.names[s] = self.namer.next()
        for s in copies:
            for d in sorted(self.w3d[s].scan.defined):
                if d not in self.names:
                    self.names[d] = self.namer.next()
        # textures named inside W3D files
        tex_wanted = {}
        for s in copies:
            for t in self.w3d[s].scan.textures:
                stem, _ext = split_ext(t)
                tex_wanted.setdefault(stem.lower(), t)
        for stem in sorted(tex_wanted):
            self._plan_texture(tex_wanted[stem], short=True)
        # unresolved external names
        for s in copies:
            for x in sorted(self.w3d[s].scan.external()):
                e = self.w3d.get(x)
                if x not in defined_somewhere and (e is None or e.status == "missing"):
                    self.info("W3D %s refers to '%s', which is in no file of the mod; assuming the ruleset has it"
                              % (self.w3d[s].path, x))
        for s in sorted(self.w3d):
            e = self.w3d[s]
            if e.status == "copy":
                new = self.names[s]
                self._record("Model", "copied", s, new, "art/w3d/%s.w3d" % new.lower())
            elif e.status == "base":
                self._record("Model", "reference", s, None, e.path)
            elif e.status == "missing":
                if any(search.w3d_stem("Model", t) == s for t in self.needs["Model"]) or \
                        any(t.partition(".")[2] == s for t in self.needs["Anim"]):
                    self._record("Model", "missing", s, None, e.path, self._hint("Model", s))

    def _texture_files(self, token):
        """The files the engine would load for a texture request: all variants (dds, tga) it may use, from the first
        directory (localized folder, then Art/Textures) that has one."""
        paths = search.texture_paths(token, self.language)
        per_dir = {}
        for p in paths:
            per_dir.setdefault(p.rsplit("/", 1)[0], []).append(p)
        for d, ps in per_dir.items():
            found = [p for p in ps if self.mod.exists(p)]
            if found:
                return found
        return []

    def _plan_texture(self, token, short):
        stem, ext = split_ext(token)
        orig = stem
        key = stem.lower()
        if key in self._tex_status:
            return
        cands = self._texture_files(token)
        if not cands:
            self._tex_status[key] = "missing"
            self._record("Texture", "missing", token, None, "art/textures/%s.*" % key, self._hint("Texture", token))
            return
        if all(self._same_as_base(p) for p in cands):
            self._tex_status[key] = "base"
            self._record("Texture", "reference", orig, None, cands[0])
            return
        if key.startswith("zhc") and not self.zhc:
            self._zhc_lost.append(orig)
        if short:
            new = self.namer.next("ZHC" if key.startswith("zhc") and self.zhc else "")
        else:
            new = "%s_%s" % (self.tag, stem)
            if key.startswith("zhc") and self.zhc:
                new = "ZHC%s_%s" % (self.tag, stem)
        self.tex[key] = new
        self._tex_status[key] = "copy"
        for p in cands:
            pext = os.path.splitext(p)[1]
            out = "art/textures/%s%s" % (new.lower(), pext)
            self.files[out] = functools.partial(self.mod.read, p)
            self._record("Texture", "copied", orig, new, out)

    # ---- audio ------------------------------------------------------------------------------------------
    def _find_audio(self, kind, token):
        """The file the engine plays for this reference: the localized copy first, then the plain one."""
        return search.first_existing(self.mod, search.audio_paths(kind, token, self.language, self.settings))

    def _plan_audio(self):
        folders = {"AudioFile": "data/audio/sounds/", "TrackFile": "data/audio/tracks/",
                   "SpeechFile": "data/audio/speech/"}
        for kind in ("AudioFile", "TrackFile", "SpeechFile"):
            for tok in sorted(self.needs[kind]):
                orig = self.needs[kind][tok]
                path = self._find_audio(kind, tok)
                self._take_audio(kind, tok, orig, path, folders[kind], strip_ext=(kind == "AudioFile"))

    def _take_audio(self, kind, tok, orig, path, folder, strip_ext):
        if not path:
            tried = search.audio_paths(kind, tok, self.language, self.settings)
            self._record(kind, "missing", orig, None, folder + orig,
                         "searched " + ", ".join(tried))
            return
        # keep it a reference when the ruleset has the same file at the same path
        if self._same_as_base(path):
            self._record(kind, "reference", orig, None, path)
            return
        fname = path.rsplit("/", 1)[1]
        base, ext = split_ext(fname)
        new_file = "%s_%s%s" % (self.tag, base, ext)
        new_token = "%s_%s" % (self.tag, orig) if not strip_ext else "%s_%s" % (self.tag, orig)
        out = folder + new_file.lower()
        self.files[out] = functools.partial(self.mod.read, path)
        self.audio[kind][tok] = new_token
        self._record(kind, "copied", orig, new_token, out)

    # ---- running ------------------------------------------------------------------------------------------
    def plan(self):
        self._plan_w3d()
        for tok in sorted(self.needs["Texture"]):
            self._plan_texture(self.needs["Texture"][tok], short=False)
        self._plan_audio()
        # a name that two copied files both define cannot stay in both (the package allows one definition):
        # one file keeps it (the file named like it, else the first), the others get names of their own
        owners = {}
        for s in sorted(self.w3d):
            if self.w3d[s].status == "copy":
                for d in self.w3d[s].scan.defined:
                    owners.setdefault(d, []).append(s)
        local = {}
        for d in sorted(owners):
            ss = owners[d]
            if len(ss) > 1:
                keep = d if d in ss else ss[0]
                self.duplicates.append((d, ss, keep))
                for s in ss:
                    if s != keep:
                        local.setdefault(s, {})[d] = self.namer.next()
        for s in sorted(self.w3d):
            e = self.w3d[s]
            if e.status != "copy":
                continue
            textures = {}
            for t in e.scan.textures:
                stem, ext = split_ext(t)
                new = self.tex.get(stem.lower())
                if new:
                    textures[t.lower()] = new + ext
            names = self.names
            if s in local:
                names = dict(self.names)
                names.update(local[s])
            w3d.rename(e.chunks, names, textures)
            self.files["art/w3d/%s.w3d" % self.names[s].lower()] = w3d.serialize(e.chunks)
        if self._zhc_lost:
            self.warn("%d house-colour texture(s) (names starting with ZHC) lost their team colour because package "
                      "file names must start with the tag: %s" % (len(self._zhc_lost),
                                                                 ", ".join(sorted(self._zhc_lost)[:6])))

    # ---- mapping for INI values --------------------------------------------------------------------------------
    def map_token(self, kind, token):
        low = token.lower()
        if kind == "Model":
            if "." in low:
                head, _, rest = token.partition(".")      # "FILE.PART": the part before the dot names the file
                new = self.names.get(head.lower())
                return None if new is None else new + "." + rest
            return self.names.get(low)
        if kind == "Anim":
            hier, dot, anim = token.partition(".")
            if not dot:
                return self.names.get(low)
            h = self.names.get(hier.lower(), hier)
            a = self.names.get(anim.lower(), anim)
            return None if (h == hier and a == anim) else h + "." + a
        if kind == "Texture":
            stem, ext = split_ext(token)
            new = self.tex.get(stem.lower())
            return None if new is None else new + ext
        if kind in self.audio:
            return self.audio[kind].get(low)
        return None
