"""``zharmy validate``: check a package against the rules of docs/ARMY_PACKAGES.md.

The engine applies the same rules when it loads a package (and refuses a package that breaks one). Each message
names the rule it comes from. Errors make the package invalid; warnings are things the check could not decide
(for example references into the ruleset when no ``--base`` is given).
"""

import json
import os
import re

from . import ini as inimod
from . import refs as refsmod
from . import schema, scb as scbmod, strings as stringsmod, w3d
from .gamedata import BLOCK_FILE, KIND_OF_BLOCK, GameData
from .package import FORMAT, Package, PackageError
from .vfs import Vfs, glob_matcher

TAG_RE = re.compile(r"^[A-Z][A-Z0-9]{1,5}$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9.-]{2,63}$")
DEF_KINDS = frozenset(schema.DEFINITION_KINDS)
ALLOWED_BLOCKS = frozenset(BLOCK_FILE) - {"AIData"}
KNOWN_RULESETS = ("zerohour", "starter")
SOFT = refsmod.SKIP_TOKENS


class Result:
    def __init__(self, path):
        self.path = path
        self.errors = []
        self.warnings = []
        self.notes = []
        self.manifest = None

    @property
    def ok(self):
        return not self.errors

    def error(self, rule, msg):
        self.errors.append((rule, msg))

    def warn(self, msg):
        if msg not in self.warnings:
            self.warnings.append(msg)

    def note(self, msg):
        self.notes.append(msg)

    def as_dict(self):
        return {"package": self.path, "ok": self.ok,
                "errors": [{"rule": r, "message": m} for r, m in self.errors],
                "warnings": self.warnings, "notes": self.notes}


def format_result(res):
    lines = ["%s: %s" % (os.path.basename(res.path), "OK" if res.ok else "INVALID")]
    for rule, msg in res.errors:
        lines.append("  error [%s]: %s" % (rule, msg))
    for msg in res.warnings:
        lines.append("  warning: " + msg)
    for msg in res.notes:
        lines.append("  note: " + msg)
    return "\n".join(lines) + "\n"


def _load_base(base_paths, mod_archives=None):
    if not base_paths:
        return None, None
    vfs = Vfs()
    exclude = glob_matcher(mod_archives) if mod_archives else None
    for p in base_paths:
        vfs.add_tree(p, overwrite=False, exclude=exclude)
    return vfs, GameData(vfs, "base")


def validate(path, base_paths=None, zhc_names=True, others=(), mod_archives=None):
    # ZHC<TAG> names for house-colour textures are part of rule 4; the argument is kept for old callers
    zhc_names = True
    res = Result(path)
    try:
        pkg = Package(path)
    except PackageError as exc:
        res.error("container", str(exc))
        return res
    try:
        _check_container(pkg, res)
        manifest = None
        try:
            manifest = pkg.manifest()
        except PackageError as exc:
            res.error("manifest", str(exc))
        if manifest is not None:
            res.manifest = manifest
            _check_manifest(pkg, manifest, res)
            if res.manifest and TAG_RE.match(str(manifest.get("tag", ""))):
                base_vfs, base_data = _load_base(base_paths, mod_archives)
                _check_content(pkg, manifest, res, base_vfs, base_data, zhc_names, others)
    finally:
        pkg.close()
    return res


# ---------------------------------------------------------------------------------------------------------------
def _check_container(pkg, res):
    for p in pkg.problems:
        res.error("container", p)
    seen = set()
    for key, info in pkg.infos.items():
        name = info.filename
        if name.startswith("/") or re.match(r"^[A-Za-z]:", name) or "\\" in name:
            res.error("container", "entry %r: names use '/' separators and are relative" % name)
        if ".." in name.split("/"):
            res.error("container", "entry %r: '..' is not allowed" % name)
        if info.flag_bits & 0x1:
            res.error("container", "entry %r is encrypted" % name)
        if info.compress_type not in (0, 8):
            res.error("container", "entry %r uses compression method %d (only stored and deflate are allowed)"
                      % (name, info.compress_type))
        if not name.isascii() and not (info.flag_bits & 0x800):
            res.error("container", "entry %r: non-ASCII names must be flagged UTF-8" % name)
        if info.file_size > 0xFFFFFFFF or info.compress_size > 0xFFFFFFFF:
            res.error("container", "entry %r needs ZIP64" % name)
        low = key
        if low.startswith("data/ini/") or low == "data/ini":
            res.error("layout", "entry %r: a package must not contain Data/INI" % name)
        elif low.startswith("data/") and not low.startswith("data/audio/"):
            res.error("layout", "entry %r: a package must not contain Data/<Language> or other game data folders"
                      % name)
        elif low.startswith("maps/"):
            res.error("layout", "entry %r: a package must not contain Maps" % name)
        seen.add(low)
    if len(pkg.infos) > 65535:
        res.error("container", "more than 65535 entries (ZIP64)")


def _check_manifest(pkg, m, res):
    def need(key, typ):
        if key not in m:
            res.error("manifest", "missing key %r" % key)
            return None
        if not isinstance(m[key], typ):
            res.error("manifest", "%r must be %s" % (key, typ.__name__))
            return None
        return m[key]

    fmt = m.get("format")
    if not isinstance(fmt, int) or isinstance(fmt, bool):
        res.error("manifest", "format must be an integer")
    elif fmt > FORMAT:
        res.error("manifest", "format %d is newer than this tool understands (%d)" % (fmt, FORMAT))
    elif fmt < 1:
        res.error("manifest", "format must be 1")
    pid = need("id", str)
    if pid is not None and not ID_RE.match(pid):
        res.error("manifest", "id %r does not match [a-z0-9][a-z0-9.-]{2,63}" % pid)
    tag = need("tag", str)
    if tag is not None and not TAG_RE.match(tag):
        res.error("manifest", "tag %r does not match [A-Z][A-Z0-9]{1,5}" % tag)
    for key in ("name", "version"):
        v = need(key, str)
        if v is not None and not v.strip():
            res.error("manifest", "%r is empty" % key)
    req = need("requires", list)
    if req is not None:
        for r in req:
            if r not in KNOWN_RULESETS:
                res.error("manifest", "requires: unknown ruleset %r (v1 knows %s)" % (r, ", ".join(KNOWN_RULESETS)))
    facs = need("factions", list)
    if facs is not None:
        if not facs:
            res.error("manifest", "factions is empty")
        for i, f in enumerate(facs):
            if not isinstance(f, dict):
                res.error("manifest", "factions[%d] must be an object" % i)
                continue
            for key, typ in (("playerTemplate", str), ("side", str), ("displayName", str), ("ai", bool)):
                if key not in f or not isinstance(f[key], typ):
                    res.error("manifest", "factions[%d].%s is missing or has the wrong type" % (i, key))
    for key in ("authors",):
        if key in m and not isinstance(m[key], list):
            res.error("manifest", "%r must be a list" % key)
    h = m.get("contentHash")
    if not isinstance(h, str) or not re.match(r"^sha256:[0-9a-f]{64}$", h):
        res.error("manifest", "contentHash must be 'sha256:<64 hex digits>'")
    else:
        actual = pkg.compute_hash()
        if actual != h:
            res.error("manifest", "contentHash does not match the contents (the package was edited or damaged): "
                                  "manifest %s, actual %s" % (h[:22] + "...", actual[:22] + "..."))
    if "converter" in m and isinstance(m["converter"], dict):
        for w in m["converter"].get("warnings", []) or []:
            res.note("converter warning: %s" % w)


# ---------------------------------------------------------------------------------------------------------------
def _stem(path):
    return os.path.splitext(path.rsplit("/", 1)[-1])[0]


def _check_content(pkg, m, res, base_vfs, base_data, zhc, others):
    tag = m["tag"]
    prefix = (tag + "_").lower()
    requires = [r for r in m.get("requires", []) if isinstance(r, list) or isinstance(r, str)]
    rel = bool(requires)
    sides = {}
    for f in m.get("factions", []):
        if isinstance(f, dict) and isinstance(f.get("side"), str):
            sides[f["side"].lower()] = f
            if not f["side"].lower().startswith(prefix):
                res.error("rule 3", "side %r must start with %s_" % (f["side"], tag))
    # --- the other packages -------------------------------------------------------------------------
    other_infos = []
    for o in others:
        try:
            op = Package(o)
            om = op.manifest()
            other_infos.append((o, om, op))
        except PackageError as exc:
            res.warn("--with %s could not be read: %s" % (o, exc))
    for o, om, op in other_infos:
        if om.get("id") == m.get("id"):
            res.error("loading", "id %r is also used by %s" % (m.get("id"), os.path.basename(o)))
        if str(om.get("tag", "")).upper() == tag.upper():
            res.error("rule 1", "tag %s is also used by %s" % (tag, os.path.basename(o)))
        for n in op.names():
            if n != "manifest.json" and pkg.has(n) and not n.startswith("army/"):
                res.error("no shadowing", "file %s is also in %s" % (n, os.path.basename(o)))
    # --- files ----------------------------------------------------------------------------------------
    names = pkg.names()
    ini_files = [n for n in names if n.startswith("army/ini/") and n.endswith(".ini")]
    asset_files = [n for n in names if n.startswith("art/") or n.startswith("data/audio/")]
    for n in asset_files:
        base = n.rsplit("/", 1)[-1]
        if not (base.startswith(tag.lower()) or (zhc and base.startswith("zhc" + tag.lower()))):
            res.error("rule 4", "file name %s does not start with the tag %s" % (n, tag))
        if base_vfs is not None and base_vfs.exists(n):
            res.error("no shadowing", "%s already exists in the ruleset" % n)
    for n in names:
        known = (n == "manifest.json" or n in ("army/scripts/skirmish.scb", "army/strings.str")
                 or n in ini_files or n in asset_files or n.rsplit("/", 1)[-1].startswith(("license", "readme")))
        if not known:
            res.note("ignored entry %s" % n)
    # --- W3D -------------------------------------------------------------------------------------------
    defined_w3d = {}
    pkg_textures = set()
    w3d_textures = []
    for n in names:
        if n.startswith("art/textures/"):
            pkg_textures.add(_stem(n).lower())
    for n in asset_files:
        if n.startswith("art/w3d/") and n.endswith(".w3d"):
            try:
                chunks = w3d.parse(pkg.read(n))
                sc = w3d.scan(chunks)
            except w3d.W3dError as exc:
                res.error("rule 4", "%s is not a valid W3D file: %s" % (n, exc))
                continue
            for d in sc.defined:
                if not d.startswith(tag.lower()):
                    res.error("rule 4", "%s defines the W3D name %r, which does not start with %s" % (n, d, tag))
                if len(d) > 15:
                    res.error("rule 4", "%s: W3D name %r is longer than 15 characters" % (n, d))
                if d in defined_w3d and defined_w3d[d] != n:
                    res.error("rule 4", "W3D name %r is defined by both %s and %s" % (d, defined_w3d[d], n))
                defined_w3d.setdefault(d, n)
            for t in sc.textures:
                w3d_textures.append((n, t))
            for x in sc.external():
                if x.startswith(tag.lower()) and x not in defined_w3d:
                    # may be defined by a file we have not read yet; checked below
                    pass
    for n, t in w3d_textures:
        stem = os.path.splitext(t)[0].lower()
        if stem.startswith(tag.lower()) or (zhc and stem.startswith("zhc" + tag.lower())):
            if stem not in pkg_textures:
                res.error("rule 4", "%s uses texture %s, which is not in the package" % (n, t))
        elif not stem.startswith("zhc"):
            _need_ruleset_file(res, base_vfs, rel, ["art/textures/%s.dds" % stem, "art/textures/%s.tga" % stem],
                               "texture %s used by %s" % (t, n))
    # --- strings -----------------------------------------------------------------------------------------
    labels = {}
    if pkg.has("army/strings.str"):
        dups = []
        labels = stringsmod.parse_str(pkg.read("army/strings.str"), dups)
        for d in dups:
            res.error("rule 6", "label %s is defined twice in Strings.str" % d)
        for label in labels:
            if not (label.lower().startswith(prefix) or label.lower().startswith(tag.lower() + ":")):
                res.error("rule 6", "label %s does not start with %s_ or %s:" % (label, tag, tag))
    # --- definitions ------------------------------------------------------------------------------------
    parser = inimod.Parser()
    pkg_defs = {}
    top_nodes = []
    ai_blocks = []
    for n in ini_files:
        parsed = parser.parse(pkg.read(n), n)
        for line, msg in parsed.errors:
            res.error("definitions", "%s line %d: %s" % (n, line, msg))
        for block in parsed.blocks:
            if block.name == "AIData":
                ai_blocks.append((n, block))
                continue
            if block.name not in ALLOWED_BLOCKS and block.name != "ObjectReskin":
                res.error("definitions", "%s: block type %s cannot be added by a package" % (n, block.name))
                continue
            toks = block.args.split()
            if not toks:
                res.error("definitions", "%s line %d: %s has no name" % (n, block.line, block.name))
                continue
            name = toks[0]
            if not name.lower().startswith(prefix):
                res.error("rule 1", "%s: %s %s does not start with %s_" % (n, block.name, name, tag))
            kind = KIND_OF_BLOCK[block.name]
            key = name.lower()
            if key in pkg_defs.setdefault(kind, {}):
                res.error("definitions", "%s %s is defined twice in the package" % (kind, name))
            pkg_defs[kind][key] = block
            top_nodes.append((kind, name, block))
            if base_data is not None and base_data.has(kind, name):
                res.error("rule 1", "%s %s redefines a definition of the ruleset" % (kind, name))
            for o, om, op in other_infos:
                pass
    # factions
    for f in m.get("factions", []):
        if not isinstance(f, dict):
            continue
        t = pkg_defs.get("PlayerTemplate", {}).get(str(f.get("playerTemplate", "")).lower())
        if t is None:
            res.error("manifest", "faction template %s is not defined in the package" % f.get("playerTemplate"))
        else:
            ts = (t.first("Side") or "").strip()
            if ts.lower() != str(f.get("side", "")).lower():
                res.error("rule 3", "PlayerTemplate %s has Side %s but the manifest says %s"
                          % (f.get("playerTemplate"), ts, f.get("side")))
    for key, t in pkg_defs.get("PlayerTemplate", {}).items():
        ts = (t.first("Side") or "").strip().lower()
        if ts and ts not in sides:
            res.error("rule 3", "PlayerTemplate %s uses side %s, which is not a side of a faction in the manifest"
                      % (t.args.split()[0], ts))
    # objects: side
    for key, obj in pkg_defs.get("Object", {}).items():
        side = (obj.first("Side") or "").strip()
        if side and side.lower() not in sides:
            res.error("rule 3", "Object %s is on side %s, which is not a side of this package"
                      % (obj.args.split()[0], side))
    # references
    unchecked_ruleset = [False]
    for kind, name, block in top_nodes:
        _check_refs(res, kind, name, block, pkg, pkg_defs, base_vfs, base_data, rel, prefix, tag, labels,
                    unchecked_ruleset, zhc)
    if unchecked_ruleset[0] and base_data is None:
        res.warn("references to ruleset definitions were not checked: give --base <ruleset data>")
    # AIData
    if ai_blocks:
        if len(ai_blocks) > 1:
            res.error("rule 5", "AIData is given %d times; a package has a single AIData block" % len(ai_blocks))
        for n, block in ai_blocks:
            for c in block.children:
                if c.name not in ("SideInfo", "SkirmishBuildList") or not c.is_block:
                    res.error("rule 5", "%s: AIData may only hold SideInfo and SkirmishBuildList, found %s"
                              % (n, c.name))
                    continue
                side = c.args.split()[0] if c.args.split() else ""
                if side.lower() not in sides:
                    res.error("rule 5", "%s: %s %s is not a side of this package" % (n, c.name, side))
                if c.name == "SkirmishBuildList":
                    for st in c.children:
                        if st.name == "Structure" and st.args.split():
                            _check_def_ref(res, "Object", st.args.split()[0], "SkirmishBuildList %s" % side,
                                           pkg_defs, base_data, rel, prefix, True, unchecked_ruleset)
                if c.name == "SideInfo":
                    refsmod.rewrite_fields(c, lambda kind, tok: None)
    # skirmish scripts
    scb = None
    if pkg.has("army/scripts/skirmish.scb"):
        try:
            scb = scbmod.read_scb(pkg.read("army/scripts/skirmish.scb"))
        except (scbmod.ScbError, ValueError, IndexError) as exc:
            res.error("scripts", "Army/Scripts/Skirmish.scb cannot be read: %s" % exc)
        if scb is not None:
            allowed = {"skirmish" + s for s in sides}
            for pname, _d in scb.players:
                if pname.lower() not in allowed:
                    res.error("scripts", "Skirmish.scb has a script list for player %s; only %s are allowed"
                              % (pname, ", ".join("Skirmish" + s for s in sorted(sides))))
            for t in scb.teams:
                owner = str(t.get("teamOwner", ""))
                if owner.lower() not in allowed:
                    res.error("scripts", "team %s belongs to %s, not to a skirmish player of this package"
                              % (t.get("teamName"), owner))
    have_lists = set()
    for n, block in ai_blocks:
        for c in block.children:
            if c.name == "SkirmishBuildList" and c.args.split():
                have_lists.add(c.args.split()[0].lower())
    for f in m.get("factions", []):
        if isinstance(f, dict) and f.get("ai") is True:
            side = str(f.get("side", "")).lower()
            if side not in have_lists:
                res.error("manifest", "faction %s says ai: true but AIData has no SkirmishBuildList %s"
                          % (f.get("playerTemplate"), f.get("side")))
            if scb is None or ("skirmish" + side) not in {p.lower() for p, _d in scb.players}:
                res.error("manifest", "faction %s says ai: true but Skirmish.scb has no list for Skirmish%s"
                          % (f.get("playerTemplate"), f.get("side")))
    for o, om, op in other_infos:
        op.close()


def _need_ruleset_file(res, base_vfs, rel, candidates, what):
    if not rel:
        res.error("rule 2", "%s is not in the package (the package relies on no ruleset)" % what)
        return
    if base_vfs is None:
        res.warn("%s was not checked: give --base <ruleset data>" % what)
        return
    if not any(base_vfs.exists(c) for c in candidates):
        res.error("rule 2", "%s is in neither the package nor the ruleset" % what)


def _check_def_ref(res, kind, token, where, pkg_defs, base_data, rel, prefix, strict, unchecked):
    low = token.lower()
    if low in SOFT:
        return
    if low in pkg_defs.get(kind, {}):
        return
    if low.startswith(prefix):
        res.error("rule 2", "%s refers to %s %s, which the package does not define" % (where, kind, token))
        return
    if not strict:
        return
    if not rel:
        res.error("rule 2", "%s refers to %s %s, which is not in the package (requires is empty)"
                  % (where, kind, token))
        return
    if base_data is None:
        unchecked[0] = True
        return
    if not base_data.has(kind, token):
        res.error("rule 2", "%s refers to %s %s, which is in neither the package nor the ruleset"
                  % (where, kind, token))


def _check_refs(res, kind, name, block, pkg, pkg_defs, base_vfs, base_data, rel, prefix, tag, labels, unchecked, zhc):
    where = "%s %s" % (kind, name)
    for hk, tok in refsmod.header_refs(block):
        _check_def_ref(res, hk, tok, where, pkg_defs, base_data, rel, prefix, True, unchecked)
    for r in refsmod.field_refs(block):
        tok = r.token
        low = tok.lower()
        strict_one = refsmod.strict_ref(r) and not refsmod.is_keyword(tok)
        handled = False
        for k in r.kinds:
            if k in DEF_KINDS:
                handled = True
                if any(low in pkg_defs.get(kk, {}) for kk in r.kinds if kk in DEF_KINDS):
                    break
                _check_def_ref(res, k, tok, "%s (%s)" % (where, r.field.name), pkg_defs, base_data, rel, prefix,
                               strict_one, unchecked)
                break
            if k == "Label":
                handled = True
                if low in {l.lower() for l in labels}:
                    break
                if low.startswith(prefix) or low.startswith(tag.lower() + ":"):
                    res.error("rule 6", "%s uses label %s, which is not in Strings.str" % (where, tok))
                elif not rel:
                    res.error("rule 6", "%s uses label %s, which is not in Strings.str (requires is empty)"
                              % (where, tok))
                break
            if k in schema.ASSET_KINDS:
                handled = True
                _check_asset_ref(res, k, tok, where, pkg, base_vfs, rel, tag, zhc)
                break
            if k == "Side":
                break


def _check_asset_ref(res, kind, tok, where, pkg, base_vfs, rel, tag, zhc):
    t = tok.lower()
    if kind == "Model":
        stem = t
        cands = ["art/w3d/%s.w3d" % stem]
    elif kind == "Anim":
        stem = t.partition(".")[2] or t
        cands = ["art/w3d/%s.w3d" % stem]
    elif kind == "Texture":
        stem = os.path.splitext(t)[0]
        cands = ["art/textures/%s.dds" % stem, "art/textures/%s.tga" % stem]
    elif kind == "AudioFile":
        stem = t
        cands = ["data/audio/sounds/%s.wav" % t, "data/audio/sounds/%s.mp3" % t]
    elif kind == "TrackFile":
        stem = t
        cands = ["data/audio/tracks/%s" % t]
    else:
        stem = t
        cands = ["data/audio/speech/%s" % t, "data/audio/speech/%s.wav" % t]
    if any(pkg.has(c) for c in cands):
        return
    if stem.startswith(tag.lower()) or (zhc and stem.startswith("zhc" + tag.lower())):
        res.error("rule 4", "%s refers to %s %s, which is not in the package" % (where, kind.lower(), tok))
        return
    if kind == "Anim" and "." not in t:
        return
    _need_ruleset_file(res, base_vfs, rel, cands, "%s: %s %s" % (where, kind.lower(), tok))
