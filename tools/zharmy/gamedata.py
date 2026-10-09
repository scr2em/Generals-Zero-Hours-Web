"""Load the INI definitions of a game or mod from a virtual file system, in the engine's load order.

``INI::loadFileDirectory("Data\\\\INI\\\\Object")`` loads ``Data/INI/Object.ini`` and then every ``.ini`` below
``Data/INI/Object/`` (files of the folder itself first, then the sub folders, each sorted case-insensitively).
``GameEngine::init`` calls it for the stems listed in ``LOAD_STEMS`` (``Default/<Name>`` before ``<Name>`` where
the engine does so). A later definition of the same name replaces an earlier one for the stores that clear
their entry first (FXList, ObjectCreationList) and is merged over it by the others; the converter treats the last
definition as the effective one and reports every redefinition.
"""

import hashlib

from . import ini as inimod
from .vfs import norm

_PARSE_CACHE = {}      # content digest -> parsed IniFile (shared between the mod and the base load)

# block type -> definition kind
KIND_OF_BLOCK = {
    "Object": "Object", "ObjectReskin": "Object", "Weapon": "Weapon", "Locomotor": "Locomotor", "Armor": "Armor",
    "DamageFX": "DamageFX", "CommandButton": "CommandButton", "CommandSet": "CommandSet", "FXList": "FXList",
    "ObjectCreationList": "OCL", "ParticleSystem": "ParticleSystem", "Upgrade": "Upgrade", "Science": "Science",
    "SpecialPower": "SpecialPower", "AudioEvent": "Audio", "MusicTrack": "Audio", "DialogEvent": "Audio",
    "MappedImage": "MappedImage", "PlayerTemplate": "PlayerTemplate", "CrateData": "Crate",
    "EvaEvent": "Eva", "Rank": "Rank", "Animation": "Anim2D",
}

# kinds a package may add, with the INI file (Army/INI/<file>) each block type is written to
BLOCK_FILE = {
    "Object": "Object", "ObjectReskin": "Object", "Weapon": "Weapon", "Locomotor": "Locomotor", "Armor": "Armor",
    "DamageFX": "DamageFX", "CommandButton": "CommandButton", "CommandSet": "CommandSet", "FXList": "FXList",
    "ObjectCreationList": "ObjectCreationList", "ParticleSystem": "ParticleSystem", "Upgrade": "Upgrade",
    "Science": "Science", "SpecialPower": "SpecialPower", "AudioEvent": "SoundEffects", "MusicTrack": "Music",
    "DialogEvent": "Speech", "MappedImage": "MappedImages", "PlayerTemplate": "PlayerTemplate",
    "CrateData": "Crate", "AIData": "AIData",
}
# order in which the engine loads the types (a package's files are written in this order)
FILE_ORDER = ["Science", "PlayerTemplate", "FXList", "Weapon", "ObjectCreationList", "Locomotor", "SpecialPower",
              "DamageFX", "Armor", "Object", "Upgrade", "AIData", "Crate", "CommandButton", "CommandSet",
              "ParticleSystem", "MappedImages", "Music", "SoundEffects", "Speech", "Voice", "Eva"]

# file stems the engine loads (see GameEngine.cpp and the client subsystems), in order
LOAD_STEMS = [
    "Default/GameData", "GameData", "Default/Water", "Water", "Default/Weather", "Weather", "Default/Science",
    "Science", "Default/Multiplayer", "Multiplayer", "Default/Terrain", "Terrain", "Default/Roads", "Roads", "Rank",
    "Default/PlayerTemplate", "PlayerTemplate", "Default/FXList", "FXList", "Weapon",
    "Default/ObjectCreationList", "ObjectCreationList", "Locomotor", "Default/SpecialPower", "SpecialPower",
    "DamageFX", "Armor", "Default/Object", "Object", "Default/Upgrade", "Upgrade", "Default/AIData", "AIData",
    "Default/Crate", "Crate", "Default/CommandButton", "CommandButton", "CommandSet", "ParticleSystem",
    "Default/Music", "Music", "Default/SoundEffects", "SoundEffects", "Default/Speech", "Speech",
    "Default/Voice", "Voice", "MiscAudio", "Eva", "Animation2D",
]
MAPPED_IMAGE_DIRS = ["Data/INI/MappedImages/HandCreated"]   # plus TextureSize_<n> folders


class Definition:
    __slots__ = ("kind", "name", "key", "node", "file", "seq", "origin")

    def __init__(self, kind, name, node, file, seq, origin):
        self.kind, self.name, self.node, self.file, self.seq, self.origin = kind, name, node, file, seq, origin
        self.key = name.lower()

    def __repr__(self):
        return "<Def %s %s from %s>" % (self.kind, self.name, self.file)


class GameData:
    """All definitions of one VFS. ``defs[kind][lowercase name]`` is the effective (last) definition."""

    def __init__(self, vfs, origin="mod", log=None):
        self.vfs = vfs
        self.origin = origin
        self.defs = {}
        self.redefined = []          # (kind, name, first file, last file, same text?)
        self.ai_blocks = []          # (side_infos, build_lists) AIData node list in load order
        self.ai = []                 # top level AIData nodes
        self.errors = []             # (file, line, message)
        self.opaque = []             # (type, name, file)
        self.extensions = {}         # file -> list of notes
        self.files = []              # INI files read, in order
        self.eva = []
        self._seq = 0
        self._load()

    # ---- loading ------------------------------------------------------------------------------------------
    def _resolver(self, target, including):
        base = norm(including).rsplit("/", 1)[0] if "/" in norm(including) else ""
        for cand in ((base + "/" + target) if base else target, target, "Data/INI/" + target):
            if self.vfs.exists(cand):
                return self.vfs.read(cand)
        return None

    def _files_for_stem(self, stem):
        found = []
        top = "data/ini/" + norm(stem)
        if self.vfs.exists(top + ".ini"):
            found.append(top + ".ini")
        below = [p for p in self.vfs.walk(top) if p.endswith(".ini")]
        direct = [p for p in below if "/" not in p[len(top) + 1:]]
        nested = [p for p in below if "/" in p[len(top) + 1:]]
        found.extend(sorted(direct))
        found.extend(sorted(nested))
        return found

    def _mapped_image_files(self):
        out = []
        prefix = "data/ini/mappedimages/"
        paths = [p for p in self.vfs.walk("Data/INI/MappedImages") if p.endswith(".ini")]
        sized = sorted(p for p in paths if "/texturesize_" in p)
        hand = sorted(p for p in paths if "/handcreated/" in p)
        rest = sorted(p for p in paths if p not in sized and p not in hand)
        out.extend(sized + hand + rest)
        return out

    def _load(self):
        seen = set()
        order = []
        for stem in LOAD_STEMS:
            for p in self._files_for_stem(stem):
                if p not in seen:
                    seen.add(p)
                    order.append(p)
        for p in self._mapped_image_files():
            if p not in seen:
                seen.add(p)
                order.append(p)
        parser = inimod.Parser(self._resolver)
        for path in order:
            self.files.append(path)
            raw = self.vfs.read(path)
            digest = (hashlib.md5(raw).digest(), path)
            parsed = _PARSE_CACHE.get(digest)
            if parsed is None:
                parsed = parser.parse(raw, path)
                _PARSE_CACHE[digest] = parsed
            for line, msg in parsed.errors:
                self.errors.append((path, line, msg))
            if parsed.extensions:
                self.extensions[path] = sorted(set(parsed.extensions))
            for t, n in parsed.opaque:
                self.opaque.append((t, n, path))
            for block in parsed.blocks:
                self._register(block, path)

    def _register(self, block, path):
        if block.name == "AIData":
            self.ai.append(block)
            return
        kind = KIND_OF_BLOCK.get(block.name)
        if kind is None or block.style == "opaque":
            return
        toks = block.args.split()
        if not toks:
            return
        name = toks[0]
        self._seq += 1
        d = Definition(kind, name, block, path, self._seq, self.origin)
        table = self.defs.setdefault(kind, {})
        old = table.get(d.key)
        if old is not None:
            self.redefined.append((kind, name, old.file, path, inimod.canonical(old.node) == inimod.canonical(block)))
        table[d.key] = d

    # ---- queries ------------------------------------------------------------------------------------------
    def get(self, kind, name):
        return self.defs.get(kind, {}).get(name.lower())

    def has(self, kind, name):
        return name.lower() in self.defs.get(kind, {})

    def kinds(self):
        return self.defs.keys()

    def ordered(self, kind):
        return sorted(self.defs.get(kind, {}).values(), key=lambda d: d.seq)

    # AIData -------------------------------------------------------------------------------------------------
    def side_info(self, side):
        found = None
        for top in self.ai:
            for c in top.children:
                if c.name == "SideInfo" and c.args.split()[:1] and c.args.split()[0].lower() == side.lower():
                    found = c
        return found

    def build_list(self, side):
        found = None
        for top in self.ai:
            for c in top.children:
                if c.name == "SkirmishBuildList" and c.args.split()[:1] and c.args.split()[0].lower() == side.lower():
                    found = c
        return found


# ---- INI problems ---------------------------------------------------------------------------------------------
def problem_kind(msg):
    if msg.startswith("unknown block"):
        return "unknown top level line (a block that ended early, or text outside any block)"
    if msg.startswith("missing End"):
        return "block without End"
    if msg.startswith("#unresolved-include"):
        return "#include that could not be found"
    return msg.split(":")[0][:60]


def group_ini_problems(errors):
    """errors: [(file, line, message)] -> [(kind, [errors])], most frequent kind first."""
    groups = {}
    for e in errors:
        groups.setdefault(problem_kind(e[2]), []).append(e)
    return sorted(groups.items(), key=lambda kv: -len(kv[1]))


def format_ini_problems(errors, examples=3, full=False):
    """Text summary of INI problems: per kind the count and the files, first examples as file:line, or all of
    them when ``full``."""
    if not errors:
        return "No INI problems.\n"
    lines = ["%d INI problem%s:" % (len(errors), "" if len(errors) == 1 else "s")]
    for kind, items in group_ini_problems(errors):
        files = {}
        for f, _l, _m in items:
            files[f] = files.get(f, 0) + 1
        top = ", ".join("%s (%d)" % kv for kv in sorted(files.items(), key=lambda kv: -kv[1])[:4])
        lines.append("  %s: %d in %d file%s, mostly %s" % (kind, len(items), len(files),
                                                             "" if len(files) == 1 else "s", top))
        shown = items if full else items[:examples]
        for f, l, m in shown:
            lines.append("    %s:%d: %s" % (f, l, m))
        if not full and len(items) > examples:
            lines.append("    ... and %d more" % (len(items) - examples))
    return "\n".join(lines) + "\n"
