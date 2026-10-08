#!/usr/bin/env python3
"""Cross-reference checker for a built starter pack.

    python3 validate_pack.py <built pack dir> [--repo <repo root>]

The engine loads the pack's files lazily and often substitutes defaults for a missing reference (a model that
cannot be found simply is not drawn), so a typo can hide for a long time. This tool follows every reference it
knows about and reports the ones that do not resolve:

* INI: models, textures, images, weapons, armor, locomotors, command sets/buttons, effects, particle systems,
  audio events, music, player templates, the AI build list, terrain and water textures
* strings: every label used by INI files and WND layouts must exist in ``Data/Generals.str``
* WND: images, text labels, and (when the repository is at hand) callback names the engine knows
* audio: every wave named by an event, every music file
* map: object templates, sides' factions, team unit types, referenced scripts, start waypoints
* files: TGA and W3D headers are parsed

Exit status 0 when nothing is wrong.
"""

import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from spk.datachunk import ChunkReader  # noqa: E402
from spk.mapfile import read_map  # noqa: E402
from spk.scripts import read_player_scripts  # noqa: E402
from spk.strfile import parse_str  # noqa: E402
from spk.tga import read_tga  # noqa: E402
from spk.w3d import parse_w3d  # noqa: E402
from spk.wnd import parse_wnd  # noqa: E402


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, where, message):
        self.errors.append("%s: %s" % (where, message))

    def warn(self, where, message):
        self.warnings.append("%s: %s" % (where, message))


# ------------------------------------------------------------------------------------------------- INI

class Block:
    def __init__(self, kind, name, parent=None):
        self.kind, self.name, self.parent = kind, name, parent
        self.fields = []        # (key, value, line)
        self.children = []

    def walk(self):
        yield self
        for c in self.children:
            for b in c.walk():
                yield b


SINGLE_LINE = {"LODPreset"}       # statements that are one line and have no End
MODULE_TAG = re.compile(r"ModuleTag_\w+")


def parse_ini(text, where, report):
    """A tolerant structural parse: returns the list of top level blocks."""
    top = []
    stack = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        head = line.split(None, 1)
        word = head[0]
        rest = head[1] if len(head) > 1 else ""
        if word.lower() == "end" and not rest:
            if not stack:
                report.error("%s:%d" % (where, lineno), "End without a block")
            else:
                stack.pop()
            continue
        if "=" in line.split()[0] or re.match(r"^\S+\s*=", line):
            key, _, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if MODULE_TAG.search(value):
                blk = Block(key, value, stack[-1] if stack else None)
                (stack[-1].children if stack else top).append(blk)
                stack.append(blk)
                continue
            if not stack:
                report.error("%s:%d" % (where, lineno), "field outside a block: " + line)
                continue
            stack[-1].fields.append((key, value, lineno))
            continue
        if word in SINGLE_LINE:
            continue
        # a line without '=': opens a block (kind [name])
        blk = Block(word, rest.strip(), stack[-1] if stack else None)
        (stack[-1].children if stack else top).append(blk)
        stack.append(blk)
    if stack:
        report.error(where, "unterminated block %s %s" % (stack[-1].kind, stack[-1].name))
    return top


# ------------------------------------------------------------------------------------------------- pack index

class Pack:
    def __init__(self, root, report):
        self.root = root
        self.report = report
        self.files = {}
        for dirpath, _d, names in os.walk(root):
            for n in names:
                rel = os.path.relpath(os.path.join(dirpath, n), root).replace(os.sep, "/").lower()
                self.files[rel] = os.path.join(dirpath, n)
        self.defs = {}      # kind -> {name lower: Block}
        self.images = set()
        self.strings = set()
        self.ini_blocks = []

    def has(self, rel):
        return rel.lower() in self.files

    def read(self, rel):
        with open(self.files[rel.lower()], "rb") as f:
            return f.read()

    def load(self):
        for rel in sorted(self.files):
            if rel.startswith("data/ini/") and rel.endswith(".ini") or rel.startswith("data/english/") and rel.endswith(".ini"):
                text = self.read(rel).decode("latin-1")
                for blk in parse_ini(text, rel, self.report):
                    self.ini_blocks.append((rel, blk))
                    self.defs.setdefault(blk.kind.lower(), {}).setdefault(blk.name.split()[0].lower() if blk.name else "", blk)
        for kind in ("mappedimage",):
            self.images = set(self.defs.get(kind, {}))
        if self.has("data/generals.str"):
            self.strings = set(parse_str(self.read("data/generals.str")))
        else:
            self.report.error("data/generals.str", "missing")

    def defined(self, kind, name):
        return name.lower() in self.defs.get(kind.lower(), {})


# ------------------------------------------------------------------------------------------------- checks

def values_of(block, key):
    return [(v, ln) for k, v, ln in block.fields if k.lower() == key.lower()]


def check_ini(pack):
    r = pack.report
    D = pack.defined

    def need(kind, name, where, what):
        if name and name.lower() not in ("none", "[none]", "noimage", "null") and not D(kind, name):
            r.error(where, "%s '%s' is not defined (%s)" % (what, name, kind))

    def need_file(rel, where, what):
        if not pack.has(rel):
            r.error(where, "%s: file %s is missing" % (what, rel))

    def need_string(label, where):
        if label and label.lower() != "none" and label not in pack.strings:
            r.error(where, "string label %s is missing from Generals.str" % label)

    def tex(name, where, subdir="art/textures"):
        base = name if "." in name else name + ".tga"
        if not pack.has("%s/%s" % (subdir, base)):
            r.error(where, "texture %s/%s is missing" % (subdir, base))

    for rel, blk in pack.ini_blocks:
        kind = blk.kind.lower()
        loc = lambda ln: "%s:%d" % (rel, ln)
        if kind == "object":
            for b in blk.walk():
                for k, v, ln in b.fields:
                    kl = k.lower()
                    first = v.split()[0] if v.split() else ""
                    last = v.split()[-1] if v.split() else ""
                    if kl == "model" and first.lower() != "none":
                        need_file("art/w3d/%s.w3d" % first, loc(ln), "model")
                    elif kl == "shadowtexture":
                        tex(first, loc(ln))
                    elif kl in ("buttonimage", "selectportrait"):
                        need("mappedimage", first, loc(ln), "image")
                    elif kl == "weapon":
                        need("weapon", last, loc(ln), "weapon")
                    elif kl == "armor":
                        need("armor", first, loc(ln), "armor")
                    elif kl == "locomotor":
                        need("locomotor", last, loc(ln), "locomotor")
                    elif kl == "commandset":
                        need("commandset", first, loc(ln), "command set")
                    elif kl in ("deathfx", "fxlist"):
                        need("fxlist", first, loc(ln), "effect list")
                    elif kl == "damagefx":
                        pass
                    elif kl == "object" and b.kind.lower() == "prerequisites":
                        need("object", first, loc(ln), "prerequisite object")
                    elif kl == "displayname":
                        need_string(first, loc(ln))
                    elif kl.startswith("voice") or kl.startswith("sound"):
                        need("audioevent", first, loc(ln), "audio event")
        elif kind == "weapon":
            for k, v, ln in blk.fields:
                if k.lower() in ("firefx", "projectiledetonationfx"):
                    need("fxlist", v, loc(ln), "effect list")
        elif kind == "fxlist":
            for b in blk.walk():
                names = values_of(b, "Name")
                for v, ln in names:
                    if b.kind.lower() == "particlesystem":
                        need("particlesystem", v, loc(ln), "particle system")
                    elif b.kind.lower() == "sound":
                        need("audioevent", v, loc(ln), "audio event")
        elif kind == "commandbutton":
            for k, v, ln in blk.fields:
                kl = k.lower()
                if kl == "object":
                    need("object", v.split()[0], loc(ln), "object")
                elif kl == "buttonimage":
                    need("mappedimage", v, loc(ln), "image")
                elif kl in ("textlabel", "descriptlabel"):
                    need_string(v.strip('"'), loc(ln))
        elif kind == "commandset":
            for k, v, ln in blk.fields:
                if k.isdigit():
                    need("commandbutton", v, loc(ln), "command button")
        elif kind == "particlesystem":
            for v, ln in values_of(blk, "ParticleName"):
                tex(v, loc(ln))
        elif kind == "mappedimage":
            for v, ln in values_of(blk, "Texture"):
                tex(v, loc(ln))
        elif kind == "audioevent":
            for k, v, ln in blk.fields:
                if k.lower() in ("sounds", "soundsnight", "soundsevening", "soundsmorning", "attack", "decay"):
                    for name in re.split(r"[\s,=]+", v):
                        if name:
                            need_file("data/audio/sounds/%s.wav" % name, loc(ln), "sound")
        elif kind == "musictrack":
            for v, ln in values_of(blk, "Filename"):
                need_file("data/audio/tracks/%s" % v, loc(ln), "music file")
        elif kind == "miscaudio":
            for k, v, ln in blk.fields:
                need("audioevent", v, loc(ln), "audio event")
        elif kind == "playertemplate":
            for k, v, ln in blk.fields:
                kl = k.lower()
                if kl == "startingbuilding" or kl.startswith("startingunit"):
                    need("object", v, loc(ln), "object")
                elif kl in ("loadscreenmusic", "scorescreenmusic"):
                    need("musictrack", v, loc(ln), "music track")
                elif kl.endswith("image"):
                    need("mappedimage", v, loc(ln), "image")
                elif kl.startswith("purchasesciencecommandset") or kl == "specialpowershortcutcommandset":
                    need("commandset", v, loc(ln), "command set")
                elif kl in ("displayname", "armytooltip", "features"):
                    need_string(v.strip('"'), loc(ln))
        elif kind == "aidata":
            for b in blk.walk():
                if b.kind.lower() == "structure":
                    need("object", b.name, "%s (build list)" % rel, "structure")
        elif kind == "terrain":
            for v, ln in values_of(blk, "Texture"):
                if not pack.has("art/terrain/" + v.lower()):
                    r.error(loc(ln), "terrain sheet art/terrain/%s is missing" % v)
        elif kind == "waterset":
            for key in ("SkyTexture", "WaterTexture"):
                for v, ln in values_of(blk, key):
                    tex(v, loc(ln))
        elif kind == "watertransparency":
            for k, v, ln in blk.fields:
                if k.lower().startswith("skyboxtexture") or k.lower() == "standingwatertexture":
                    tex(v, loc(ln))
        elif kind == "controlbarscheme":
            for b in blk.walk():
                for v, ln in values_of(b, "ImageName"):
                    need("mappedimage", v, loc(ln), "image")
        elif kind == "mouse":
            pass
    # PlayerTemplate must offer a side the map file can use
    if not D("playertemplate", "FactionObserver"):
        r.error("data/ini/playertemplate.ini", "FactionObserver is required by the engine")


def check_wnds(pack, repo):
    r = pack.report
    known_callbacks = None
    if repo:
        known_callbacks = set()
        for rel in ("GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GameWindowManagerScript.cpp",):
            pass
        lex = os.path.join(repo, "Core/GameEngine/Source/GameClient/GUI/GameWindowManager.cpp")
        for dirpath, _d, names in os.walk(os.path.join(repo, "GeneralsMD/Code/GameEngine/Source/GameClient")):
            for n in names:
                if n.endswith(".cpp") and ("FunctionLexicon" in n):
                    text = open(os.path.join(dirpath, n), encoding="latin-1").read()
                    known_callbacks.update(re.findall(r'\{\s*NAMEKEY_INVALID\s*,\s*"([^"]+)"', text))
        for dirpath, _d, names in os.walk(os.path.join(repo, "Core/GameEngine/Source/GameClient")):
            for n in names:
                if n.endswith(".cpp") and ("FunctionLexicon" in n):
                    text = open(os.path.join(dirpath, n), encoding="latin-1").read()
                    known_callbacks.update(re.findall(r'\{\s*NAMEKEY_INVALID\s*,\s*"([^"]+)"', text))
        if not known_callbacks:
            known_callbacks = None
    for rel in sorted(pack.files):
        if not rel.startswith("window/") or not rel.endswith(".wnd"):
            continue
        try:
            _layout, windows = parse_wnd(pack.read(rel))
        except ValueError as e:
            r.error(rel, "does not parse: %s" % e)
            continue
        for w in windows:
            for x in w.walk():
                if x.text and x.text.lower() not in ("none",) and x.text not in pack.strings:
                    r.error(rel, "window %s: text label %s is missing from Generals.str" % (x.name, x.text))
                for key, block in x.draw_blocks.items():
                    for image in re.findall(r"IMAGE:\s*([^,]+)", block):
                        image = image.strip()
                        if image.lower() != "noimage" and image not in pack.images and image.lower() not in {i.lower() for i in pack.images}:
                            r.error(rel, "window %s: image %s is not a mapped image" % (x.name, image))
                if known_callbacks is not None:
                    for cb in x.callbacks.values():
                        if cb and cb.lower() != "[none]" and cb not in known_callbacks:
                            r.warn(rel, "window %s: callback %s not found in the function lexicon tables" % (x.name, cb))


def check_binaries(pack):
    r = pack.report
    for rel in sorted(pack.files):
        try:
            if rel.endswith(".tga"):
                w, h, px = read_tga(pack.read(rel))
                if w <= 0 or h <= 0 or len(px) != w * h * 4:
                    r.error(rel, "bad TGA")
                if rel.startswith("art/terrain/") and (w % 64 or h % 64):
                    r.error(rel, "terrain sheets must be a multiple of 64 pixels (%dx%d)" % (w, h))
            elif rel.endswith(".w3d"):
                parse_w3d(pack.read(rel))
                name = os.path.basename(rel)[:-4]
                if len(name) > 15:
                    r.error(rel, "model names are limited to 15 characters")
            elif rel.endswith(".wav"):
                data = pack.read(rel)
                if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
                    r.error(rel, "not a RIFF/WAVE file")
        except Exception as e:  # noqa: BLE001
            r.error(rel, "unreadable: %s" % e)


def check_map(pack):
    r = pack.report
    maps = [rel for rel in pack.files if rel.startswith("maps/") and rel.endswith(".map")]
    if not maps:
        r.error("maps", "no map in the pack")
    # player templates by side
    sides = {}
    for blk in pack.defs.get("playertemplate", {}).values():
        sides[blk.name.lower()] = [v for k, v, _ in blk.fields if k.lower() == "side"]
    skirmish_scripts = None
    if pack.has("data/scripts/skirmishscripts.scb"):
        reader = ChunkReader(pack.read("data/scripts/skirmishscripts.scb"))
        skirmish_scripts = {}
        for name, _v, off, size in reader.chunks():
            if name == "PlayerScriptsList":
                skirmish_scripts["lists"] = read_player_scripts(reader, off, size)
            elif name == "ScriptsPlayers":
                cur = reader.cursor(off)
                has_dict = cur.int()
                names = []
                for _ in range(cur.int()):
                    names.append(cur.ascii())
                    if has_dict:
                        cur.dict()
                skirmish_scripts["players"] = names
            elif name == "ScriptTeams":
                cur = reader.cursor(off)
                teams = []
                while cur.pos < off + size:
                    teams.append(cur.dict())
                skirmish_scripts["teams"] = teams
    for rel in maps:
        data = read_map(pack.read(rel))
        where = rel
        order = data["order"]
        if order.index("WorldInfo") > order.index("SidesList") or order.index("SidesList") > order.index("ObjectsList"):
            r.error(where, "chunk order: WorldInfo, SidesList, ObjectsList is required")
        waypoints = set()
        for o in data["objects"]:
            if o.props.get("waypointID") is not None and "waypointID" in o.props:
                waypoints.add(o.props.get("waypointName"))
            elif o.name and not o.name.startswith("*") and not pack.defined("object", o.name):
                r.error(where, "object template %s is not defined" % o.name)
        starts = [w for w in waypoints if re.match(r"Player_\d+_Start$", w or "")]
        if len(starts) < 2:
            r.error(where, "needs at least two Player_N_Start waypoints (found %d)" % len(starts))
        factions = {}
        for d, build in data["sides"]:
            faction = d.get("playerFaction") or ""
            if faction and not pack.defined("playertemplate", faction):
                r.error(where, "side %r uses undefined faction %s" % (d.get("playerName"), faction))
            factions[d.get("playerName")] = faction
        if not any(f for f in factions.values() if f and f not in ("FactionCivilian", "FactionObserver")):
            r.error(where, "no skirmish side for a playable faction")
        for cls in data["classes"]:
            terrain = pack.defs.get("terrain", {}).get(cls.name.lower())
            if terrain is None:
                r.error(where, "terrain class %s is not defined in Terrain.ini" % cls.name)
                continue
            sheet = [v for k, v, _ in terrain.fields if k.lower() == "texture"]
            if not sheet or not pack.has("art/terrain/" + sheet[0]):
                r.error(where, "terrain class %s has no sheet" % cls.name)
        # tiles must point inside the loaded source tiles
        limit = data["num_bitmap_tiles"]
        bad = [t for t in data["tiles"] if (t >> 2) >= limit or t < 0]
        if bad:
            r.error(where, "%d cells refer to tiles beyond the %d loaded" % (len(bad), limit))
        name_key = data["world"].get("mapName") if "world" in data else None
        if name_key:
            strfile = os.path.dirname(rel) + "/map.str"
            if not pack.has(strfile):
                r.error(where, "mapName %s needs %s" % (name_key, strfile))
            else:
                labels = parse_str(pack.read(strfile))
                if name_key not in labels:
                    r.error(where, "%s does not define %s" % (strfile, name_key))
        if not pack.has(rel[:-4] + ".tga"):
            r.warn(where, "no preview picture (%s.tga)" % rel[:-4])
    # a release engine lists the standard maps from Maps\\MapCache.ini (it scans the folder only with -buildmapcache)
    if maps:
        if not pack.has("maps/mapcache.ini"):
            r.error("maps/mapcache.ini", "missing: no map would reach the skirmish setup screen")
        else:
            cache = pack.read("maps/mapcache.ini").decode("latin-1")
            for rel in maps:
                key = rel.replace("/", "\\")
                qp = "".join(c if c.isalnum() else "_%02X" % ord(c) for c in key)
                m = re.search(r"MapCache %s\s(.*?)\bEND\b" % re.escape(qp), cache, re.S | re.I)
                if not m:
                    r.error("maps/mapcache.ini", "no entry for %s" % key)
                    continue
                body = pack.read(rel)
                crc = 0
                for b in body:
                    crc = ((crc << 1) + b + (1 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
                size = re.search(r"fileSize\s*=\s*(\d+)", m.group(1))
                got = re.search(r"fileCRC\s*=\s*(\d+)", m.group(1))
                if not size or int(size.group(1)) != len(body) or not got or int(got.group(1)) != crc:
                    r.error("maps/mapcache.ini", "size or CRC of %s does not match the file" % key)
    if skirmish_scripts is not None:
        script_names = set()
        for sl in skirmish_scripts.get("lists", []):
            for s in sl:
                if "name" in s:
                    script_names.add(s["name"])
        for t in skirmish_scripts.get("teams", []):
            owner = t.get("teamOwner")
            if owner not in skirmish_scripts.get("players", []):
                r.error("skirmishscripts.scb", "team %s belongs to unknown player %s" % (t.get("teamName"), owner))
            for i in range(1, 8):
                unit = t.get("teamUnitType%d" % i)
                if unit and not pack.defined("object", unit):
                    r.error("skirmishscripts.scb", "team %s: unit %s is not defined" % (t.get("teamName"), unit))
            for key in ("teamProductionCondition", "teamOnCreateScript"):
                v = t.get(key)
                if v and v not in script_names:
                    r.error("skirmishscripts.scb", "team %s: %s '%s' is not a script of the file" % (t.get("teamName"), key, v))
    else:
        r.warn("data/scripts", "no SkirmishScripts.scb: the computer opponent would have no teams")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("--repo", default=None)
    args = ap.parse_args(argv)
    root = args.pack
    if os.path.isdir(os.path.join(root, "StarterPack")):
        root = os.path.join(root, "StarterPack")
    repo = args.repo
    if repo is None:
        try:
            from spk.engine_schema import find_repo_root
            repo = find_repo_root()
        except RuntimeError:
            repo = None
    report = Report()
    pack = Pack(root, report)
    pack.load()
    check_ini(pack)
    check_wnds(pack, repo)
    check_binaries(pack)
    check_map(pack)
    for w in report.warnings:
        print("warning: " + w)
    for e in report.errors:
        print("ERROR: " + e)
    print("%d files, %d errors, %d warnings" % (len(pack.files), len(report.errors), len(report.warnings)))
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
