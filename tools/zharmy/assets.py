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

from . import w3d

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
        self._audio_index = None
        self._tex_status = {}
        self._zhc_lost = []

    # ---- input ------------------------------------------------------------------------------------------
    def add(self, kind, token):
        if not token or token.lower() in ("none", "nosound"):
            return
        self.needs[kind].setdefault(token.lower(), token)

    # ---- helpers ----------------------------------------------------------------------------------------
    def _same_as_base(self, path):
        return self.use_base and self.mod.same_file(self.base, path)

    def _record(self, kind, status, old, new=None, path=None):
        self.report[status].append({"kind": kind, "name": old, "new": new, "path": path})

    # ---- W3D ----------------------------------------------------------------------------------------------
    def _w3d_queue(self):
        queue = deque()
        for tok in sorted(self.needs["Model"]):
            queue.append((tok, True))
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
            path = "art/w3d/%s.w3d" % stem
            if not self.mod.exists(path):
                self.w3d[stem] = W3dEntry("missing", path)
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
                tex_wanted.setdefault(stem.lower(), stem)
        for stem in sorted(tex_wanted):
            self._plan_texture(stem, tex_wanted[stem], short=True)
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
                if s in self.needs["Model"] or any(t.partition(".")[2] == s for t in self.needs["Anim"]):
                    self._record("Model", "missing", s, None, e.path)

    def _plan_texture(self, stem, orig, short):
        key = stem.lower()
        if key in self._tex_status:
            return
        cands = [("art/textures/%s%s" % (key, ext)) for ext in TEXTURE_EXTS if self.mod.exists("art/textures/%s%s" % (key, ext))]
        if not cands:
            self._tex_status[key] = "missing"
            self._record("Texture", "missing", orig, None, "art/textures/%s.*" % key)
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
            ext = os.path.splitext(p)[1]
            out = "art/textures/%s%s" % (new.lower(), ext)
            self.files[out] = functools.partial(self.mod.read, p)
            self._record("Texture", "copied", orig, new, out)

    # ---- audio ------------------------------------------------------------------------------------------
    def _audio_paths(self):
        if self._audio_index is None:
            idx = {}
            for p in self.mod.all_paths():
                if p.startswith("data/audio/"):
                    parts = p.split("/")
                    if len(parts) >= 4:
                        idx.setdefault((parts[2], parts[-1]), []).append(p)
            self._audio_index = idx
        return self._audio_index

    def _find_audio(self, folder, filename):
        """Best path for ``filename`` in a data/audio/<folder>, preferring the chosen language."""
        base = "data/audio/%s/" % folder
        direct = base + filename
        lang = base + self.language + "/" + filename
        if self.mod.exists(lang):
            return lang
        if self.mod.exists(direct):
            return direct
        others = sorted(self._audio_paths().get((folder, filename), []))
        return others[0] if others else None

    def _plan_audio(self):
        for tok in sorted(self.needs["AudioFile"]):
            orig = self.needs["AudioFile"][tok]
            path = None
            for ext in (".wav", ".mp3"):
                path = self._find_audio("sounds", tok + ext)
                if path:
                    break
            self._take_audio("AudioFile", tok, orig, path, "data/audio/sounds/", strip_ext=True)
        for kind, folder in (("TrackFile", "tracks"), ("SpeechFile", "speech")):
            for tok in sorted(self.needs[kind]):
                orig = self.needs[kind][tok]
                path = self._find_audio(folder, tok)
                if not path and "." not in tok:
                    for ext in (".wav", ".mp3"):
                        path = self._find_audio(folder, tok + ext)
                        if path:
                            break
                self._take_audio(kind, tok, orig, path, "data/audio/%s/" % folder, strip_ext=False)

    def _take_audio(self, kind, tok, orig, path, folder, strip_ext):
        if not path:
            self._record(kind, "missing", orig, None, folder + orig)
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
            stem, _ext = split_ext(self.needs["Texture"][tok])
            self._plan_texture(stem, stem, short=False)
        self._plan_audio()
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
            w3d.rename(e.chunks, self.names, textures)
            self.files["art/w3d/%s.w3d" % self.names[s].lower()] = w3d.serialize(e.chunks)
        if self._zhc_lost:
            self.warn("%d house-colour texture(s) (names starting with ZHC) lost their team colour because package "
                      "file names must start with the tag: %s" % (len(self._zhc_lost),
                                                                 ", ".join(sorted(self._zhc_lost)[:6])))

    # ---- mapping for INI values --------------------------------------------------------------------------------
    def map_token(self, kind, token):
        low = token.lower()
        if kind == "Model":
            new = self.names.get(low)
            return new
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
