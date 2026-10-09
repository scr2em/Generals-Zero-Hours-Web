"""``zharmy convert``: turn one army of a mod into a ``.zharmy`` package.

Pipeline
--------
1. Build two virtual file systems: the *ruleset* (``--base``) and the *mod as played* (ruleset plus the mod folders
   and archives on top, with the engine's rules for which file wins), and load the INI definitions of both.
2. Start from the faction's ``PlayerTemplate`` and follow every reference (``schema``). A definition stays a
   reference to the ruleset when the ruleset has the same definition with the same text and everything it points
   to is also unchanged (computed as a fixed point, so a stock weapon that the mod edited drags the units that use
   it into the package). Objects of the faction's side and the template itself are always copied: the side is
   renamed, so its objects must move with it.
3. Everything copied is renamed ``<TAG>_<name>``; model, texture and sound files are collected, W3D files get
   short generated names (see ``assets``); labels move to ``Strings.str`` as ``<TAG>:<label>``; the faction's
   ``SideInfo``/``SkirmishBuildList`` and its skirmish scripts are renamed and added.
4. The result is written as ``manifest.json`` + ``army/...`` entries, and a report says what was copied, what
   stayed a reference, and what could not be carried over.
"""

import os
import re
from collections import OrderedDict, deque

from . import __version__
from . import assets as assetsmod
from . import ini as inimod
from . import refs as refsmod
from . import schema, scb as scbmod, strings as stringsmod
from .gamedata import BLOCK_FILE, KIND_OF_BLOCK, GameData
from .package import write_package
from .vfs import Vfs, glob_matcher, norm

TAG_RE = re.compile(r"^[A-Z][A-Z0-9]{1,5}$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9.-]{2,63}$")
DEF_KINDS = frozenset(schema.DEFINITION_KINDS)
ASSET_KINDS = frozenset(schema.ASSET_KINDS)


class ConvertError(Exception):
    pass


class Options:
    def __init__(self, **kw):
        self.tag = None
        self.faction = None
        self.requires = "zerohour"
        self.id = None
        self.name = None
        self.version = "1.0.0"
        self.description = None
        self.authors = []
        self.license = "As stated by the mod's authors; for personal use."
        self.source_mod = None
        self.source_version = None
        self.source_url = None
        self.language = None
        self.zhc_names = True
        for k, v in kw.items():
            if not hasattr(self, k):
                raise TypeError("unknown option %s" % k)
            setattr(self, k, v)


def build_vfs(mod_paths, base_paths, mod_archives=None):
    """Returns (mod_vfs, base_vfs or None).

    The mod file system is what the game sees with the mod installed: the base layers with the mod on top.
    Normally ``mod_paths`` are separate folders / archives that go on top of the base (they win, like ``-mod``).
    When the mod is installed *into* the game folder, give that folder as ``--base`` (and as the mod) and name the
    mod's archives with ``mod_archives`` (glob patterns on the path relative to the folder, or the file name): the
    ruleset is then the folder without those files, the mod file system is the whole folder under the engine's
    rule for one folder (loose files first, then archives in alphabetical order, the first archive holding a file
    wins; that is why mod archives are often called ``!something.big``).
    """
    exclude = glob_matcher(mod_archives) if mod_archives else None
    real = lambda p: os.path.realpath(p)
    base_real = {real(p) for p in (base_paths or [])}
    base = None
    if base_paths:
        base = Vfs()
        for p in base_paths:
            base.add_tree(p, overwrite=False, exclude=exclude)
    mod = Vfs()
    if base_paths:
        if exclude is not None:
            for p in base_paths:
                mod.add_tree(p, overwrite=False)       # the whole installed game
        else:
            mod.layers.extend(base.layers)
    for p in mod_paths:
        if real(p) in base_real:
            continue                                   # the installed folder is already part of the base layers
        # a folder on top of a ruleset is a mod loaded with overwrite; a lone folder is a game as installed
        mod.add_tree(p, overwrite=bool(base_paths))
    return mod, base


class Report:
    def __init__(self):
        self.warnings = []
        self.infos = []
        self.data = OrderedDict()

    def warn(self, msg):
        if msg not in self.warnings:
            self.warnings.append(msg)

    def info(self, msg):
        if msg not in self.infos:
            self.infos.append(msg)


def slug(text):
    return re.sub(r"[^a-z0-9]+", "", text.lower())


class Context:
    """Everything that does not depend on which faction is converted: the file systems, the loaded definitions,
    the string tables and the comparison with the ruleset. Built once; ``convert-all`` reuses it."""

    def __init__(self, mod_paths, base_paths, requires="zerohour", language=None, mod_archives=None,
                 progress=None):
        self.progress = progress or (lambda m: None)
        self.mod_paths = list(mod_paths)
        self.base_paths = list(base_paths or [])
        self.requires = requires
        self.use_base = requires != "none"
        self.language = language
        self.warnings = []
        if self.use_base and not self.base_paths:
            raise ConvertError("--requires %s needs the ruleset data (--base); use --requires none for a fully "
                               "self-contained package" % requires)
        real = [os.path.realpath(p) for p in self.mod_paths]
        if self.base_paths and not mod_archives and set(real) & {os.path.realpath(p) for p in self.base_paths}:
            self.warnings.append("the mod folder and --base are the same folder and --mod-archives is not given: "
                                 "every definition looks like a ruleset definition. Name the mod's archives with "
                                 "--mod-archives '<glob>'.")
        self.progress("reading archive directories")
        self.mod_vfs, self.base_vfs = build_vfs(self.mod_paths, self.base_paths, mod_archives)
        self.progress("loading mod definitions (%d archives/folders)" % len(self.mod_vfs.layers))
        self.mod = GameData(self.mod_vfs, "mod")
        self.base = None
        if self.base_vfs is not None:
            self.progress("loading ruleset definitions")
            self.base = GameData(self.base_vfs, "base")
        for vfs in (self.mod_vfs, self.base_vfs):
            for path, msg in (vfs.problems if vfs is not None else ()):
                w = "archive %s was skipped: %s" % (path, msg)
                if w not in self.warnings:
                    self.warnings.append(w)
        self.progress("%d mod INI files, %d definitions" % (len(self.mod.files),
                                                            sum(len(t) for t in self.mod.defs.values())))
        self.mod_strings = stringsmod.load_strings(self.mod_vfs, language)
        self.base_strings = (stringsmod.load_strings(self.base_vfs, language)
                             if self.base_vfs is not None else stringsmod.StringTable())
        self.ref_cache = {}
        self._identity_ready = False
        self._scb = None

    def skirmish_scb(self):
        """(parsed SkirmishScripts.scb or None, problem text or None)"""
        if self._scb is None:
            path = "data/scripts/skirmishscripts.scb"
            if not self.mod_vfs.exists(path):
                self._scb = (None, "no Data/Scripts/SkirmishScripts.scb in the mod: the computer gets no unit "
                                   "scripts")
            else:
                try:
                    self._scb = (scbmod.read_scb(self.mod_vfs.read(path)), None)
                except (scbmod.ScbError, ValueError, IndexError, Exception) as exc:
                    self._scb = (None, "SkirmishScripts.scb could not be read (%s); no AI scripts carried over" % exc)
        return self._scb

    # references --------------------------------------------------------------------------------------------
    def refs_of(self, d):
        key = (d.kind, d.key)
        if key not in self.ref_cache:
            self.ref_cache[key] = (refsmod.field_refs(d.node), refsmod.header_refs(d.node))
        return self.ref_cache[key]

    def targets(self, kinds, token):
        out = []
        for k in kinds:
            if k in DEF_KINDS:
                d = self.mod.get(k, token)
                if d is not None:
                    out.append(d)
        return out

    def def_edges(self, d):
        fields, headers = self.refs_of(d)
        out = []
        for r in fields:
            out.extend(self.targets(r.kinds, r.token))
        for kind, tok in headers:
            t = self.mod.get(kind, tok)
            if t is not None:
                out.append(t)
        return out

    # identity with the ruleset -------------------------------------------------------------------------------
    def prepare_identity(self):
        if self._identity_ready:
            return
        self._identity_ready = True
        self.cand = set()
        self.bad0 = set()
        self.reverse = {}
        if not self.use_base or self.base is None:
            return
        self.progress("comparing the mod's definitions with the ruleset")
        for kind, table in self.mod.defs.items():
            btable = self.base.defs.get(kind, {})
            for key, d in table.items():
                b = btable.get(key)
                if b is None:
                    continue
                if inimod.canonical(d.node) == inimod.canonical(b.node):
                    self.cand.add((kind, key))
        for k in list(self.cand):
            d = self.mod.defs[k[0]][k[1]]
            if self._assets_differ(d):
                self.bad0.add(k)
        for k in self.cand:
            if k in self.bad0:
                continue
            d = self.mod.defs[k[0]][k[1]]
            for t in self.def_edges(d):
                tk = (t.kind, t.key)
                if tk not in self.cand:
                    self.bad0.add(k)
                    break
                self.reverse.setdefault(tk, []).append(k)
        self.progress("%d of %d definitions that exist in the ruleset are unchanged"
                      % (len(self.cand - self.bad0), len(self.cand)))

    def identical_for(self, must_copy):
        """Definitions that stay references: unchanged, and nothing they point to is changed or must be copied."""
        self.prepare_identity()
        bad = set(self.bad0)
        queue = deque(k for k in must_copy if k in self.cand)
        bad.update(queue)
        while queue:
            k = queue.popleft()
            for dep in self.reverse.get(k, ()):
                if dep not in bad:
                    bad.add(dep)
                    queue.append(dep)
        return self.cand - bad

    def _assets_differ(self, d):
        fields, _h = self.refs_of(d)
        for r in fields:
            for kind in r.kinds:
                for path in Converter._asset_paths(kind, r.token):
                    if self.mod_vfs.exists(path) and not self.mod_vfs.same_file(self.base_vfs, path):
                        return True
        return False


class Converter:
    def __init__(self, ctx, options, progress=None):
        self.ctx = ctx
        self.o = options
        self.progress = progress or ctx.progress
        self.rep = Report()
        for w in ctx.warnings:
            self.rep.warn(w)
        if not options.tag or not TAG_RE.match(options.tag):
            raise ConvertError("tag %r must be 2-6 characters, [A-Z][A-Z0-9]*" % options.tag)
        if options.id is not None and not ID_RE.match(options.id):
            raise ConvertError("id %r does not match [a-z0-9][a-z0-9.-]{2,63}" % options.id)
        self.tag = options.tag
        self.prefix = options.tag + "_"
        self.use_base = ctx.use_base
        self.mod_paths = ctx.mod_paths
        self.mod_vfs, self.base_vfs = ctx.mod_vfs, ctx.base_vfs
        self.mod, self.base = ctx.mod, ctx.base
        self.mod_strings, self.base_strings = ctx.mod_strings, ctx.base_strings
        self._ref_cache = ctx.ref_cache
        self.o.requires = ctx.requires
        self.scb = None
        self._scb_cache = None

    # ------------------------------------------------------------------------------------------------------
    def run(self, output):
        o, rep = self.o, self.rep
        for f, line, msg in self.mod.errors[:200]:
            if True:
                rep.info("INI problem in %s line %d: %s" % (f, line, msg))
        tmpl = self.mod.get("PlayerTemplate", o.faction)
        if tmpl is None:
            names = sorted(d.name for d in self.mod.defs.get("PlayerTemplate", {}).values())
            raise ConvertError("PlayerTemplate %r not found. Templates: %s" % (o.faction, ", ".join(names)))
        side = (tmpl.node.first("Side") or "").strip()
        if not side:
            raise ConvertError("PlayerTemplate %s has no Side" % tmpl.name)
        self.side = side
        self.new_side = self.prefix + side
        playable = (tmpl.node.first("PlayableSide") or "No").strip().lower() == "yes"
        if not playable:
            rep.warn("PlayerTemplate %s is not playable in the mod (PlayableSide is not Yes)" % tmpl.name)
        self.tmpl = tmpl
        self.ai_roots = []
        self._prepare_ai()
        self._compute_identity()
        self._walk()
        self._name_maps()
        self._plan_assets()
        self._plan_strings()
        files = self._emit()
        manifest = self._manifest(files)
        written = write_package(output, manifest, files)
        self.manifest = written
        self.files = files
        return self._finish_report(output)

    # ---- AI inputs ------------------------------------------------------------------------------------------
    def _prepare_ai(self):
        o = self.o
        self.side_info = self.mod.side_info(self.side)
        self.build_list = self.mod.build_list(self.side)
        self.scb_list = None
        self.scb_teams = []
        self.scb_player = None
        self.scb_scripts_roots = []
        if self.build_list is None:
            self.rep.warn("no SkirmishBuildList for side %s in AIData: the computer cannot play this army" % self.side)
        if self.side_info is None:
            self.rep.info("no SideInfo for side %s in AIData" % self.side)
        self.scb, problem = self.ctx.skirmish_scb()
        if problem:
            self.rep.warn(problem)
        if self.scb is not None:
            for cand in ("Skirmish" + self.side, self.tmpl.name, "Skirmish" + self.tmpl.name):
                sl, teams, pname = scbmod.extract_for_player(self.scb, cand)
                if sl is not None:
                    self.scb_list, self.scb_teams, self.scb_player = sl, teams, pname
                    break
            if self.scb_list is None:
                self.rep.warn("SkirmishScripts.scb has no script list for Skirmish%s" % self.side)
            else:
                import copy
                r = scbmod.Renamer(self.prefix, {})
                r.script_list(copy.deepcopy(self.scb_list))
                for t in self.scb_teams:
                    r.team(t, self.scb_player, self.scb_player)
                self.scb_scripts_roots = list(r.used)
        # structure objects of the build list are roots of the closure
        if self.build_list is not None:
            for st in self.build_list.children:
                if st.name == "Structure" and st.args.split():
                    self.ai_roots.append(("Object", st.args.split()[0]))
        if self.side_info is not None:
            b = self.side_info.first("BaseDefenseStructure1")
            if b:
                self.ai_roots.append(("Object", b.split()[0]))
            for c in self.side_info.children:
                if c.is_block:
                    for s in c.all("Science"):
                        if s.args.split():
                            self.ai_roots.append(("Science", s.args.split()[0]))

    # ---- references of definitions ------------------------------------------------------------------------------
    def _refs_of(self, d):
        return self.ctx.refs_of(d)

    def _compute_identity(self):
        self.must_copy = set()
        self.must_copy.add(("PlayerTemplate", self.tmpl.key))
        for d in self.mod.defs.get("Object", {}).values():
            s = (d.node.first("Side") or "").strip()
            if s.lower() == self.side.lower():
                self.must_copy.add(("Object", d.key))
        self.identical = self.ctx.identical_for(self.must_copy) if self.use_base else set()

    @staticmethod
    def _asset_paths(kind, token):
        t = token.lower()
        if kind == "Model":
            return ["art/w3d/%s.w3d" % t]
        if kind == "Anim":
            return ["art/w3d/%s.w3d" % t.partition(".")[2]] if "." in t else []
        if kind == "Texture":
            stem = os.path.splitext(t)[0]
            return ["art/textures/%s.dds" % stem, "art/textures/%s.tga" % stem]
        if kind == "AudioFile":
            return ["data/audio/sounds/%s.wav" % t, "data/audio/sounds/%s.mp3" % t]
        if kind == "TrackFile":
            return ["data/audio/tracks/%s" % t]
        if kind == "SpeechFile":
            return ["data/audio/speech/%s" % t]
        return []

    # ---- the walk ------------------------------------------------------------------------------------------------------
    def _walk(self):
        self.progress("following references")
        self.copy = OrderedDict()          # (kind, key) -> Def
        self.refs = OrderedDict()          # (kind, key) -> Def kept as references
        self.unresolved = []
        self.labels = OrderedDict()        # lower -> original label
        self.asset_requests = []           # (kind, token)
        self.unknown_fields = OrderedDict()
        self.removed_modules = OrderedDict()
        queue = deque([self.tmpl])
        for kind, name in self.ai_roots + self.scb_scripts_roots:
            d = self.mod.get(kind, name)
            if d is not None:
                queue.append(d)
        seen = set()
        while queue:
            d = queue.popleft()
            k = (d.kind, d.key)
            if k in seen:
                continue
            seen.add(k)
            if k in self.identical and k not in self.must_copy:
                self.refs[k] = d
                continue
            self.copy[k] = d
            fields, headers = self._refs_of(d)
            for kind, tok in headers:
                t = self.mod.get(kind, tok)
                if t is not None:
                    queue.append(t)
            for r in fields:
                found = False
                for kind in r.kinds:
                    if kind in DEF_KINDS:
                        t = self.mod.get(kind, r.token)
                        if t is not None:
                            queue.append(t)
                            found = True
                    elif kind in ASSET_KINDS:
                        self.asset_requests.append((kind, r.token))
                        found = True
                    elif kind == "Label":
                        self.labels.setdefault(r.token.lower(), r.token)
                        found = True
                    elif kind == "Side":
                        found = True
                if not found and self._strict_missing(r):
                    self.unresolved.append((d.kind, d.name, r.field.name, r.token, "/".join(r.kinds)))
            unk = refsmod.unknown_fields(d.node)
            if unk:
                self.unknown_fields[(d.kind, d.name)] = sorted(set(unk))
        # redefinitions in the mod of things we use
        for kind, name, f1, f2, same in self.mod.redefined:
            if (kind, name.lower()) in self.copy and not same:
                self.rep.info("%s %s is defined more than once (%s, %s); the last definition is used"
                              % (kind, name, f1, f2))

    @staticmethod
    def _strict_missing(r):
        return refsmod.strict_ref(r) and not refsmod.is_keyword(r.token)

    # ---- names -----------------------------------------------------------------------------------------------------------
    def _name_maps(self):
        self.defmap = {}
        for (kind, key), d in self.copy.items():
            self.defmap.setdefault(kind, {})[key] = self.prefix + d.name
        # a stock definition that the mod changed is reported
        self.modified_stock = []         # the mod edited a definition the ruleset has
        self.dependent_stock = []        # unchanged itself, copied because something it points to is changed
        if self.base is not None:
            for (kind, key), d in self.copy.items():
                b = self.base.get(kind, key)
                if b is None or (kind, key) in self.must_copy:
                    continue
                if inimod.canonical(b.node) != inimod.canonical(d.node):
                    self.modified_stock.append((kind, d.name))
                else:
                    self.dependent_stock.append((kind, d.name))
        self.sidemap = {self.side.lower(): self.new_side}

    # ---- assets ----------------------------------------------------------------------------------------------------------
    def _plan_assets(self):
        self.progress("collecting model, texture and sound files")
        ap = assetsmod.AssetPlanner(self.mod_vfs, self.base_vfs, self.tag, self.use_base,
                                    self.mod_strings.language or self.o.language or "english",
                                    self.o.zhc_names, self.rep.warn, self.rep.info)
        for kind, tok in self.asset_requests:
            ap.add(kind, tok)
        ap.plan()
        self.assets = ap
        for rec in ap.report["missing"]:
            self.rep.warn("%s file not found in the mod: %s" % (rec["kind"], rec["name"]))

    # ---- strings ----------------------------------------------------------------------------------------------------------
    def _plan_strings(self):
        self.labelmap = {}
        self.string_entries = OrderedDict()
        self.label_report = {"copied": [], "reference": [], "missing": []}
        for low, label in sorted(self.labels.items()):
            text = self.mod_strings.get(label)
            if text is None:
                if self.base_strings.has(label) and self.use_base:
                    self.label_report["reference"].append(label)
                else:
                    self.label_report["missing"].append(label)
                continue
            if self.use_base and self.base_strings.get(label) == text:
                self.label_report["reference"].append(label)
                continue
            new = "%s:%s" % (self.tag, label)
            self.labelmap[low] = new
            _t, bad = stringsmod.to_latin1(text)
            if bad:
                self.rep.warn("label %s has %d character(s) outside Latin-1; replaced with '?' (the string table "
                              "format of the engine is single byte)" % (label, bad))
            self.string_entries[new] = text
            self.label_report["copied"].append(label)
        for label in self.label_report["missing"]:
            self.rep.warn("string label %s is in no string table of the mod; the reference is left as it is" % label)

    # ---- mapping of one token ----------------------------------------------------------------------------------------
    def _mapper(self, kind, token):
        low = token.lower()
        if kind in DEF_KINDS:
            return self.defmap.get(kind, {}).get(low)
        if kind == "Side":
            return self.sidemap.get(low)
        if kind == "Label":
            return self.labelmap.get(low)
        if kind in ASSET_KINDS:
            return self.assets.map_token(kind, token)
        return None

    # ---- output ------------------------------------------------------------------------------------------------------------
    def _transform(self, d):
        node = inimod.clone(d.node)
        toks = node.args.split()
        removed = refsmod.strip_unknown_modules(node)
        if removed:
            self.removed_modules[(d.kind, d.name)] = removed
        refsmod.rewrite_fields(node, self._mapper)
        if toks:
            toks[0] = self.prefix + toks[0]
            if node.name == "ObjectReskin" and len(toks) >= 2:
                toks[1] = self.defmap.get("Object", {}).get(toks[1].lower(), toks[1])
            node.args = " ".join(toks)
        if d.kind == "Object":
            self._fix_object_side(d, node)
        if d.kind == "PlayerTemplate":
            for c in node.children:
                if c.name == "Side":
                    c.args = self.new_side
        return node

    def _reskin_side_warning(self, d):
        if d.node.name != "ObjectReskin":
            return
        cur = d
        for _ in range(20):
            parent = self.mod.get("Object", cur.node.args.split()[1]) if cur.node.name == "ObjectReskin" \
                and len(cur.node.args.split()) > 1 else None
            if parent is None:
                return
            if parent.node.name != "ObjectReskin":
                side = (parent.node.first("Side") or "").strip()
                if side and side.lower() != self.side.lower():
                    self.rep.warn("ObjectReskin %s inherits side %s from %s; a reskin cannot change its side, so "
                                  "it stays on that side"
                                  % (d.name, side, parent.name))
                return
            cur = parent

    def _fix_object_side(self, d, node):
        self._reskin_side_warning(d)
        for c in node.children:
            if c.name == "Side" and not c.is_block:
                old = c.args.strip()
                if old.lower() != self.new_side.lower():
                    if old.lower() != self.side.lower():
                        self.rep.warn("Object %s was on side %s; moved to %s" % (d.name, old, self.new_side))
                    c.args = self.new_side

    def _emit(self):
        self.progress("writing definitions")
        files = {}
        groups = {}
        for (kind, key), d in sorted(self.copy.items(), key=lambda kv: kv[1].seq):
            node = self._transform(d)
            fname = BLOCK_FILE.get(d.node.name)
            if fname is None:
                self.rep.warn("%s %s has a block type (%s) a package cannot carry; skipped"
                              % (kind, d.name, d.node.name))
                continue
            groups.setdefault(fname, []).append(node)
        # AIData
        ai = self._ai_node()
        if ai is not None:
            groups["AIData"] = [ai]
        for fname, nodes in groups.items():
            files["army/ini/%s.ini" % fname.lower()] = inimod.emit_text(nodes).encode("latin-1", "replace")
        if self.string_entries:
            files["army/strings.str"] = stringsmod.build_str(self.string_entries)
        scb_bytes = self._scb_bytes()
        if scb_bytes is not None:
            files["army/scripts/skirmish.scb"] = scb_bytes
        files.update(self.assets.files)
        return files

    def _ai_node(self):
        children = []
        if self.side_info is not None:
            si = inimod.clone(self.side_info)
            si.args = self.new_side
            si.style = " "
            refsmod.rewrite_fields(si, self._mapper)
            children.append(si)
        if self.build_list is not None:
            bl = inimod.clone(self.build_list)
            bl.args = self.new_side
            bl.style = " "
            for st in bl.children:
                if st.name == "Structure" and st.args.split():
                    toks = st.args.split()
                    new = self.defmap.get("Object", {}).get(toks[0].lower())
                    if new:
                        toks[0] = new
                    st.args = " ".join(toks)
                    st.style = " "
            children.append(bl)
        if not children:
            return None
        return inimod.Node("AIData", "", children, 0, "", " ")

    def _scb_bytes(self):
        self.ai_scripts = False
        if self.scb_list is None:
            return None
        import copy
        sl = copy.deepcopy(self.scb_list)
        new_player = "Skirmish" + self.new_side
        r = scbmod.Renamer(self.prefix, self.defmap, {self.scb_player.lower(): new_player,
                                                       self.side.lower(): self.new_side}, self.labelmap)
        r.script_list(sl)
        teams = [r.team(t, self.scb_player, new_player) for t in self.scb_teams]
        out = scbmod.Scb()
        player_dict = None
        for name, dct in self.scb.players:
            if name == self.scb_player:
                player_dict = dct
        out.players = [(new_player, player_dict)]
        out.players_version = self.scb.players_version
        out.has_dicts = self.scb.has_dicts
        out.lists = [sl]
        out.lists_version = self.scb.lists_version
        out.teams = teams
        out.teams_version = self.scb.teams_version
        self.ai_scripts = True
        self.scb_player_new = new_player
        return scbmod.write_scb(out)

    # ---- manifest ----------------------------------------------------------------------------------------------------------
    def _display_name(self):
        label = self.tmpl.node.first("DisplayName")
        if label:
            text = self.mod_strings.get(label.split()[0])
            if text:
                return text
        return self.tmpl.name

    def _manifest(self, files):
        o = self.o
        fac_name = self.tmpl.name
        base_name = fac_name[7:] if fac_name.lower().startswith("faction") and len(fac_name) > 7 else fac_name
        pkg_id = o.id or "%s.%s" % (o.tag.lower(), slug(base_name) or "army")
        if not ID_RE.match(pkg_id):
            raise ConvertError("generated id %r is not valid; give one with --id" % pkg_id)
        display = self._display_name()
        ai = bool(self.build_list is not None and self.ai_scripts)
        if self.build_list is not None and not self.ai_scripts:
            self.rep.warn("no skirmish scripts for this side: marked as humans only (ai: false)")
        mod_name = o.source_mod or os.path.basename(os.path.normpath(self.mod_paths[0])) if self.mod_paths else ""
        manifest = OrderedDict()
        manifest["format"] = 1
        manifest["id"] = pkg_id
        manifest["tag"] = o.tag
        manifest["name"] = o.name or display
        manifest["version"] = o.version
        manifest["description"] = o.description or "Converted from %s." % (mod_name or "a mod")
        manifest["authors"] = list(o.authors)
        manifest["license"] = o.license
        src = OrderedDict()
        src["mod"] = mod_name
        if o.source_version:
            src["modVersion"] = o.source_version
        if o.source_url:
            src["url"] = o.source_url
        manifest["source"] = src
        manifest["requires"] = [] if o.requires == "none" else [o.requires]
        manifest["factions"] = [OrderedDict([
            ("playerTemplate", self.prefix + self.tmpl.name),
            ("side", self.new_side),
            ("displayName", display),
            ("ai", ai),
        ])]
        manifest["converter"] = OrderedDict([("tool", "zharmy"), ("version", __version__),
                                             ("warnings", [])])
        self.ai_flag = ai
        manifest["converter"]["warnings"] = self.rep.warnings[:100]
        return manifest

    # ---- report ------------------------------------------------------------------------------------------------------------
    def _finish_report(self, output):
        rep = self.rep
        d = OrderedDict()
        m = self.manifest
        d["package"] = OrderedDict([("file", os.path.basename(output)), ("id", m["id"]), ("tag", m["tag"]),
                                    ("name", m["name"]), ("requires", m["requires"]),
                                    ("contentHash", m["contentHash"]), ("entries", len(self.files) + 1),
                                    ("faction", m["factions"][0])])
        copied = OrderedDict()
        for (kind, key), df in self.copy.items():
            copied.setdefault(kind, []).append(df.name)
        refs = OrderedDict()
        for (kind, key), df in self.refs.items():
            refs.setdefault(kind, []).append(df.name)
        d["definitionsCopied"] = copied
        d["definitionsKeptAsReferences"] = refs
        d["modifiedStockDefinitions"] = [{"kind": k, "name": n} for k, n in sorted(self.modified_stock)]
        d["copiedBecauseTheyPointToChangedDefinitions"] = [{"kind": k, "name": n}
                                                           for k, n in sorted(self.dependent_stock)]
        ar = self.assets.report
        d["assets"] = OrderedDict([
            ("copied", ar["copied"]), ("keptAsReferences", ar["reference"]), ("missing", ar["missing"])])
        d["w3dNames"] = OrderedDict(sorted(self.assets.names.items()))
        d["textureNames"] = OrderedDict(sorted(self.assets.tex.items()))
        d["strings"] = self.label_report
        d["ai"] = OrderedDict([
            ("skirmishBuildList", self.build_list is not None), ("sideInfo", self.side_info is not None),
            ("scripts", self.ai_scripts), ("scriptPlayer", getattr(self, "scb_player_new", None)),
            ("computerCanPlay", self.ai_flag)])
        notc = []
        for (kind, name), mods in self.removed_modules.items():
            notc.append({"what": "module", "in": "%s %s" % (kind, name),
                         "detail": "module(s) the engine does not have were removed: " + ", ".join(sorted(set(mods)))})
        notc.extend(self._not_carried_global())
        d["notCarried"] = notc
        d["unknownFields"] = [{"kind": k, "name": n, "fields": f} for (k, n), f in self.unknown_fields.items()]
        d["unresolvedReferences"] = [{"kind": k, "name": n, "field": f, "value": v, "expected": e}
                                     for k, n, f, v, e in self.unresolved]
        d["warnings"] = rep.warnings
        d["notes"] = rep.infos
        if self.mod.extensions:
            d["iniExtensions"] = {f: e for f, e in self.mod.extensions.items()}
        return d

    def _not_carried_global(self):
        out = []
        # Eva sounds and control bar schemes for the side are global, not additions
        for df in self.mod.defs.get("Eva", {}).values():
            for sub in df.node.children:
                if sub.is_block and (sub.first("Side") or "").strip().lower() == self.side.lower():
                    out.append({"what": "eva", "in": df.name,
                                "detail": "per-side EVA sounds for %s: the engine has one EvaEvent per message, "
                                          "a package cannot add to it" % self.side})
        for t, n, f in self.mod.opaque:
            if t == "ControlBarScheme":
                out.append({"what": "controlbar", "in": "%s %s" % (t, n),
                            "detail": "control bar schemes are global; the army uses the player's scheme"})
                break
        if self.mod.extensions:
            out.append({"what": "ini-extensions", "in": ", ".join(sorted(self.mod.extensions)),
                        "detail": "these files use #include/#define or // lines, which the stock engine does not "
                                  "understand; the converter expanded them"})
        return out


def format_report(report):
    """Human readable text of the report dict."""
    L = []
    p = report["package"]
    L.append("Package %s  (tag %s, requires %s)" % (p["id"], p["tag"], ", ".join(p["requires"]) or "nothing"))
    L.append("  faction %s  side %s  \"%s\"  computer can play: %s" % (
        p["faction"]["playerTemplate"], p["faction"]["side"], p["faction"]["displayName"], p["faction"]["ai"]))
    L.append("  %d entries, %s" % (p["entries"], p["contentHash"]))
    L.append("")
    L.append("Definitions copied into the package:")
    for kind, names in report["definitionsCopied"].items():
        L.append("  %-14s %4d  %s" % (kind, len(names), _short(names)))
    L.append("Definitions kept as references to the ruleset:")
    if not report["definitionsKeptAsReferences"]:
        L.append("  (none)")
    for kind, names in report["definitionsKeptAsReferences"].items():
        L.append("  %-14s %4d  %s" % (kind, len(names), _short(names)))
    if report["modifiedStockDefinitions"]:
        L.append("Ruleset definitions the mod changed (copied under the new tag):")
        for e in report["modifiedStockDefinitions"][:60]:
            L.append("  %s %s" % (e["kind"], e["name"]))
        if len(report["modifiedStockDefinitions"]) > 60:
            L.append("  ... %d more (see --report)" % (len(report["modifiedStockDefinitions"]) - 60))
    if report["copiedBecauseTheyPointToChangedDefinitions"]:
        L.append("Unchanged ruleset definitions copied only because they point to changed ones: %d (see --report)"
                 % len(report["copiedBecauseTheyPointToChangedDefinitions"]))
    a = report["assets"]
    L.append("Files: %d copied, %d kept as references, %d missing" % (
        len(a["copied"]), len(a["keptAsReferences"]), len(a["missing"])))
    for rec in a["missing"]:
        L.append("  missing %s: %s" % (rec["kind"], rec["name"]))
    s = report["strings"]
    L.append("Strings: %d copied, %d kept as references, %d missing" % (
        len(s["copied"]), len(s["reference"]), len(s["missing"])))
    ai = report["ai"]
    L.append("AI: build list %s, side info %s, scripts %s" % (
        "yes" if ai["skirmishBuildList"] else "no", "yes" if ai["sideInfo"] else "no", "yes" if ai["scripts"] else "no"))
    if report["notCarried"]:
        L.append("Could not be carried over:")
        for e in report["notCarried"]:
            L.append("  %s (%s): %s" % (e["what"], e["in"], e["detail"]))
    if report["unknownFields"]:
        L.append("Unknown fields (kept as written; the engine ignores or rejects them):")
        for e in report["unknownFields"][:40]:
            L.append("  %s %s: %s" % (e["kind"], e["name"], ", ".join(e["fields"])))
        if len(report["unknownFields"]) > 40:
            L.append("  ... %d more definitions" % (len(report["unknownFields"]) - 40))
    if report["unresolvedReferences"]:
        L.append("References to names found nowhere:")
        for e in report["unresolvedReferences"][:40]:
            L.append("  %s %s: %s = %s (%s)" % (e["kind"], e["name"], e["field"], e["value"], e["expected"]))
    if report["warnings"]:
        L.append("Warnings:")
        for w in report["warnings"]:
            L.append("  " + w)
    if report["notes"]:
        L.append("Notes: %d (use --report to see them)" % len(report["notes"]))
    return "\n".join(L) + "\n"


def _short(names, limit=6):
    names = sorted(names, key=str.lower)
    if len(names) <= limit:
        return ", ".join(names)
    return ", ".join(names[:limit]) + ", ... (+%d)" % (len(names) - limit)


def convert(mod_paths, base_paths, options, output, progress=None, mod_archives=None):
    ctx = Context(mod_paths, base_paths, options.requires, options.language, mod_archives, progress)
    c = Converter(ctx, options, progress)
    report = c.run(output)
    return c, report


# ---- many factions ------------------------------------------------------------------------------------------
def playable_templates(ctx):
    out = []
    for d in ctx.mod.ordered("PlayerTemplate"):
        if (d.node.first("PlayableSide") or "No").split()[0].lower() == "yes":
            out.append(d)
    return out


def derive_tags(names, prefix=None):
    """One valid, unique tag per faction name. With ``prefix``: PREFIX1, PREFIX2, ... otherwise from the names."""
    tags = []
    used = set()
    for i, name in enumerate(names, 1):
        if prefix:
            tag = "%s%d" % (prefix, i)
        else:
            core = name[7:] if name.lower().startswith("faction") and len(name) > 7 else name
            words = re.findall(r"[A-Z][a-z0-9]*|[a-z0-9]+", core)
            letters = "".join(w[0] for w in words).upper()
            if len(letters) < 2:
                letters = re.sub(r"[^A-Za-z0-9]", "", core).upper()[:3]
            tag = letters[:4] or "ARMY"
            if not tag[0].isalpha():
                tag = "A" + tag
            if len(tag) < 2:
                tag += "X"
            base, n = tag, 1
            while tag in used:
                n += 1
                tag = base[:5 - len(str(n)) + 1] + str(n)
        if not TAG_RE.match(tag):
            raise ConvertError("cannot form a valid tag from %r (got %r); use --tag-prefix with 1-5 letters" % (name, tag))
        if tag in used:
            raise ConvertError("duplicate tag %s" % tag)
        used.add(tag)
        tags.append(tag)
    return tags


def convert_all(mod_paths, base_paths, out_dir, tag_prefix=None, requires="zerohour", language=None,
                mod_archives=None, template=None, progress=None, only=None):
    """Convert every playable faction. Returns (rows, contexts) where each row is a dict for the summary table."""
    import time
    progress = progress or (lambda m: None)
    ctx = Context(mod_paths, base_paths, requires, language, mod_archives, progress)
    templates = playable_templates(ctx)
    if only:
        wanted = {o.lower() for o in only}
        templates = [t for t in templates if t.name.lower() in wanted]
    if not templates:
        raise ConvertError("no playable factions found (PlayerTemplate with PlayableSide = Yes)")
    tags = derive_tags([t.name for t in templates], tag_prefix)
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for n, (t, tag) in enumerate(zip(templates, tags), 1):
        progress("[%d/%d] %s -> tag %s" % (n, len(templates), t.name, tag))
        opts = Options(tag=tag, faction=t.name, requires=requires, language=language)
        if template is not None:
            for k, v in template.items():
                setattr(opts, k, v)
        fname = "%s_%s.zharmy" % (tag, slug(t.name[7:] if t.name.lower().startswith("faction") else t.name) or "army")
        out = os.path.join(out_dir, fname)
        row = {"faction": t.name, "tag": tag, "file": out}
        t0 = time.time()
        try:
            c = Converter(ctx, opts, progress)
            report = c.run(out)
        except ConvertError as exc:
            row["error"] = str(exc)
            rows.append(row)
            continue
        asset = lambda kinds: len({r["path"] for r in report["assets"]["copied"] if r["kind"] in kinds})
        row.update({
            "sizeMB": os.path.getsize(out) / 1048576.0,
            "objects": len(report["definitionsCopied"].get("Object", [])),
            "weapons": len(report["definitionsCopied"].get("Weapon", [])),
            "models": asset(("Model",)),
            "textures": asset(("Texture",)),
            "sounds": asset(("AudioFile", "TrackFile", "SpeechFile")),
            "warnings": len(report["warnings"]),
            "seconds": time.time() - t0,
            "report": report,
        })
        rows.append(row)
    return rows, ctx


def format_summary(rows):
    head = "%-30s %-6s %-30s %8s %7s %7s %6s %6s %6s %5s" % (
        "faction", "tag", "package", "size MB", "objects", "weapons", "models", "tex", "sounds", "warn")
    L = [head, "-" * len(head)]
    tot = dict(sizeMB=0.0, objects=0, weapons=0, models=0, textures=0, sounds=0, warnings=0)
    for r in rows:
        if "error" in r:
            L.append("%-30s %-6s FAILED: %s" % (r["faction"], r["tag"], r["error"]))
            continue
        L.append("%-30s %-6s %-30s %8.2f %7d %7d %6d %6d %6d %5d" % (
            r["faction"], r["tag"], os.path.basename(r["file"]), r["sizeMB"], r["objects"], r["weapons"],
            r["models"], r["textures"], r["sounds"], r["warnings"]))
        for k in tot:
            tot[k] += r[k]
    L.append("-" * len(head))
    L.append("%-30s %-6s %-30s %8.2f %7d %7d %6d %6d %6d %5d" % (
        "total (%d)" % len(rows), "", "", tot["sizeMB"], tot["objects"], tot["weapons"], tot["models"],
        tot["textures"], tot["sounds"], tot["warnings"]))
    return "\n".join(L) + "\n"
