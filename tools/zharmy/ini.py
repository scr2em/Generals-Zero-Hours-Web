"""Parser and writer for the Zero Hour INI dialect.

What the engine does (``Core/GameEngine/Source/Common/INI/INI.cpp``), and so what this parser does:

* one logical entry per line; everything after the first ``;`` is a comment;
* a line is split with ``strtok`` using the separators space, tab, ``=``: the first token is the field (or
  block) name, the rest are its arguments. ``Name = a b`` and ``Name a b`` mean the same;
* a top level line starts a block whose type is one of the names in ``INI.cpp``'s ``theTypeTable``; the block
  ends at a line whose first token is ``End`` (case-insensitive);
* inside a block, some fields open a nested block that needs its own ``End``: the module headers
  (``Behavior``/``Body``/``Draw``/``ClientUpdate``), ``ArmorSet``, ``WeaponSet``, ``Prerequisites``,
  ``UnitSpecificSounds``/``UnitSpecificFX``, the condition states of ``W3DModelDraw``, ``Turret``/``AltTurret``
  of AI modules, the FXList and ObjectCreationList nuggets, radius decals, AIData side blocks, and the
  ``AddModule``/``ReplaceModule``/``InheritableModule``/``OverrideableByLikeKind`` wrappers.
  ``AliasConditionState`` and ``RemoveModule`` are single lines. ``ObjectReskin <Name> <Parent>`` is a block
  type of its own;
* field and block names are case-sensitive in the engine, names of definitions are not.

Not in the stock engine, accepted here as an extension and reported: ``#include "file"``, ``#define NAME value``
and ``#undef NAME`` lines (mods that use them need a modified executable or a pre-processing tool), and lines
that begin with ``//`` (the engine would treat them as an unknown field and skip them).
"""

import re

from . import schema_data

SEP = " \t="
_SPLIT = re.compile(r"^[ \t=]*([^ \t=]+)[ \t=]*(.*?)[ \t=]*$")
_TOKEN = re.compile(r"[^ \t=]+")

# nested block openers by context ---------------------------------------------------------------------------
OBJECT_BLOCKS = frozenset(["ArmorSet", "WeaponSet", "Prerequisites", "UnitSpecificSounds", "UnitSpecificFX"])
MODULE_HEADERS = frozenset(["Behavior", "Body", "Draw", "ClientUpdate"])
WRAPPERS = frozenset(["AddModule", "ReplaceModule", "InheritableModule", "OverrideableByLikeKind"])
DRAW_BLOCKS = frozenset(["DefaultConditionState", "ConditionState", "TransitionState"])
BEHAVIOR_BLOCKS = frozenset(["Turret", "AltTurret"])
DECAL_BLOCKS = frozenset([
    "DeliveryDecal", "AttackAreaDecal", "TargetingReticleDecal", "GridDecalTemplate",
])
FX_NUGGETS = frozenset(["Sound", "RayEffect", "Tracer", "LightPulse", "ViewShake", "TerrainScorch",
                        "ParticleSystem", "FXListAtBonePos"])
OCL_NUGGETS = frozenset(["CreateObject", "CreateDebris", "ApplyRandomForce", "DeliverPayload", "FireWeapon",
                         "Attack"])
SKILLSETS = frozenset(["SkillSet1", "SkillSet2", "SkillSet3", "SkillSet4", "SkillSet5"])

# top level types we understand structurally. Everything else in theTypeTable is skipped as an opaque block.
STRUCTURED = frozenset([
    "AIData", "Armor", "AudioEvent", "CommandButton", "CommandSet", "CrateData", "DamageFX", "DialogEvent",
    "EvaEvent", "FXList", "Locomotor", "MappedImage", "MusicTrack", "Object", "ObjectCreationList",
    "ObjectReskin", "ParticleSystem", "PlayerTemplate", "Rank", "Science", "SpecialPower", "Upgrade", "Weapon",
    "Animation", "MiscAudio", "Video",
])
TOP_LEVEL = frozenset(schema_data.TOP_LEVEL_BLOCKS) | {"ObjectReskin"}


class Node:
    """A field (``children is None``) or a block."""

    __slots__ = ("name", "args", "children", "line", "file", "style")

    def __init__(self, name, args="", children=None, line=0, file="", style="="):
        self.name = name
        self.args = args
        self.children = children
        self.line = line
        self.file = file
        self.style = style      # "=" -> "Name = args", " " -> "Name args"

    @property
    def is_block(self):
        return self.children is not None

    def tokens(self):
        return _TOKEN.findall(self.args)

    def first(self, name, default=None):
        """Value of the first child field called ``name`` (exact, case-sensitive)."""
        for c in self.children or ():
            if c.name == name:
                return c.args
        return default

    def all(self, name):
        return [c for c in self.children or () if c.name == name]

    def __repr__(self):
        return "<Node %s %s%s>" % (self.name, self.args, " block" if self.is_block else "")


class IniFile:
    def __init__(self, path):
        self.path = path
        self.blocks = []          # top level blocks (Node), including opaque ones (children == [])
        self.opaque = []          # names of skipped top level blocks (type, name)
        self.errors = []          # (line, message)
        self.extensions = []      # notes about #include/#define/// used


class IniError(Exception):
    pass


def split_line(text):
    """Split a cleaned line into (name, args) like ``strtok`` does. ``text`` has no comment."""
    m = _SPLIT.match(text)
    if not m:
        return None, ""
    return m.group(1), m.group(2)


def clean(raw):
    cut = raw.find(";")
    if cut >= 0:
        raw = raw[:cut]
    return raw.replace("\t", " ").replace("\r", "").strip()


class _Line:
    __slots__ = ("no", "file", "text")

    def __init__(self, no, file, text):
        self.no, self.file, self.text = no, file, text


def preprocess(text, path, resolver=None, macros=None, depth=0, extensions=None):
    """Return the logical lines of ``text``: comments removed, ``#include``/``#define`` applied."""
    macros = {} if macros is None else macros
    extensions = [] if extensions is None else extensions
    out = []
    for no, raw in enumerate(text.split("\n"), 1):
        line = clean(raw)
        if not line:
            continue
        if line.startswith("//"):
            if "//" not in extensions:
                extensions.append("//")
            continue
        if line.startswith("#"):
            parts = line[1:].split(None, 2)
            word = parts[0].lower() if parts else ""
            if word == "include" and len(parts) >= 2:
                target = parts[1] if len(parts) == 2 else (parts[1] + " " + parts[2])
                target = target.strip().strip('"').strip("<>")
                extensions.append("#include " + target)
                if resolver is not None and depth < 16:
                    data = resolver(target, path)
                    if data is not None:
                        out.extend(preprocess(data.decode("latin-1"), target, resolver, macros, depth + 1,
                                              extensions))
                        continue
                out.append(_Line(no, path, "#unresolved-include " + target))
            elif word == "define" and len(parts) >= 2:
                macros[parts[1]] = parts[2].strip() if len(parts) > 2 else ""
                extensions.append("#define")
            elif word == "undef" and len(parts) >= 2:
                macros.pop(parts[1], None)
            continue
        if macros:
            line = _expand(line, macros)
        out.append(_Line(no, path, line))
    return out


def _expand(line, macros):
    for _ in range(8):
        changed = False

        def sub(m):
            nonlocal changed
            tok = m.group(0)
            if tok in macros:
                changed = True
                return macros[tok]
            return tok
        new = re.sub(r"[A-Za-z_][A-Za-z_0-9]*", sub, line)
        line = new
        if not changed:
            break
    return line


def _child_context(ctx, name, args):
    """Return the context of the block a field in ``ctx`` opens, or None when the line is a plain field."""
    if ctx in ("Object", "Wrapper"):
        if ctx == "Object" and name in OBJECT_BLOCKS:
            return name
        if name in MODULE_HEADERS:
            return name
        if ctx == "Object" and name in WRAPPERS:
            return "Wrapper"
        return None
    if ctx == "Draw":
        return "ConditionState" if name in DRAW_BLOCKS else ("Decal" if name in DECAL_BLOCKS else None)
    if ctx in ("Behavior", "Body", "ClientUpdate"):
        if name in BEHAVIOR_BLOCKS:
            return "Turret"
        return "Decal" if name in DECAL_BLOCKS else None
    if ctx == "FXList":
        return "Nugget" if name in FX_NUGGETS else None
    if ctx == "OCL":
        return "Nugget" if name in OCL_NUGGETS else None
    if ctx == "Nugget":
        return "Decal" if name in DECAL_BLOCKS else None
    if ctx == "AIData":
        if name == "SideInfo":
            return "SideInfo"
        if name == "SkirmishBuildList":
            return "SkirmishBuildList"
        return None
    if ctx == "SideInfo":
        return "SkillSet" if name in SKILLSETS else None
    if ctx == "SkirmishBuildList":
        return "Structure" if name == "Structure" else None
    return None


def _top_context(type_name):
    if type_name in ("Object", "ObjectReskin"):
        return "Object"
    if type_name == "FXList":
        return "FXList"
    if type_name == "ObjectCreationList":
        return "OCL"
    if type_name == "AIData":
        return "AIData"
    return "Plain"


class Parser:
    def __init__(self, resolver=None):
        self.resolver = resolver

    def parse(self, data, path="<string>"):
        if isinstance(data, bytes):
            text = data.decode("latin-1")
        else:
            text = data
        result = IniFile(path)
        macros = {}
        lines = preprocess(text, path, self.resolver, macros, 0, result.extensions)
        pos = [0]

        def peek():
            return lines[pos[0]] if pos[0] < len(lines) else None

        def parse_body(ctx, owner):
            children = []
            while True:
                ln = peek()
                if ln is None:
                    result.errors.append((owner.line, "missing End for %s %s" % (owner.name, owner.args)))
                    return children
                pos[0] += 1
                name, args = split_line(ln.text)
                if name is None:
                    continue
                if name.lower() == "end":
                    return children
                if ln.text.startswith("#unresolved-include"):
                    result.errors.append((ln.no, ln.text))
                    continue
                child_ctx = _child_context(ctx, name, args)
                if child_ctx is not None:
                    node = Node(name, args, [], ln.no, ln.file, "=")
                    node.children = parse_body(child_ctx, node)
                    children.append(node)
                else:
                    children.append(Node(name, args, None, ln.no, ln.file, "="))

        while True:
            ln = peek()
            if ln is None:
                break
            pos[0] += 1
            name, args = split_line(ln.text)
            if name is None:
                continue
            if name in TOP_LEVEL:
                node = Node(name, args, [], ln.no, ln.file, " ")
                if name in STRUCTURED:
                    node.children = parse_body(_top_context(name), node)
                else:
                    node.children = self._skip_opaque(lines, pos)
                    result.opaque.append((name, args.split()[0] if args.split() else ""))
                    node.style = "opaque"
                result.blocks.append(node)
            else:
                result.errors.append((ln.no, "unknown block %r" % name))
        return result

    @staticmethod
    def _skip_opaque(lines, pos):
        """Skip an opaque block: up to the first ``End`` whose next line starts a top level block (or EOF)."""
        raw = []
        depth = 0
        while pos[0] < len(lines):
            ln = lines[pos[0]]
            pos[0] += 1
            name, _args = split_line(ln.text)
            raw.append(ln.text)
            if name and name.lower() == "end":
                nxt = lines[pos[0]] if pos[0] < len(lines) else None
                if nxt is None:
                    break
                nname, nargs = split_line(nxt.text)
                if nname in TOP_LEVEL and "=" not in nxt.text:
                    break
        return raw


# ---------------------------------------------------------------------------------------------------------
def emit(node, indent=0, out=None):
    """Append the INI text of ``node`` to ``out`` (a list of lines) and return it."""
    if out is None:
        out = []
    pad = "  " * indent
    if node.style == "opaque":
        out.append(pad + node.name + (" " + node.args if node.args else ""))
        out.extend(node.children)
        return out
    if not node.is_block:
        out.append(pad + node.name + (" = " + node.args if node.args else ""))
        return out
    if node.style == " " or not node.args:
        head = node.name + (" " + node.args if node.args else "")
    else:
        head = node.name + " = " + node.args
    out.append(pad + head)
    for c in node.children:
        emit(c, indent + 1, out)
    out.append(pad + "End")
    return out


def emit_text(nodes):
    lines = []
    for n in nodes:
        emit(n, 0, lines)
        lines.append("")
    return "\n".join(lines).rstrip("\n") + "\n"


def canonical(node):
    """A comparison key: the node's text, case-folded, whitespace collapsed, without ``=`` signs."""
    lines = emit(node)
    return "\n".join(" ".join(_TOKEN.findall(l)).lower() for l in lines)


def walk_fields(node, ancestors=None):
    """Yield (field_node, ancestor_chain) for every plain field below a block, depth first.

    ``ancestor_chain`` starts with the top level node and ends with the block holding the field.
    """
    chain = (ancestors or ()) + (node,)
    for c in node.children or ():
        if c.is_block:
            yield from walk_fields(c, chain)
        else:
            yield c, chain


def clone(node):
    return Node(node.name, node.args,
                None if node.children is None else [clone(c) for c in node.children] if node.style != "opaque"
                else list(node.children),
                node.line, node.file, node.style)
