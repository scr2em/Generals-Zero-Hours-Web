#!/usr/bin/env python3
"""Throwaway test-package builder for the engine side of army packages (docs/ARMY_PACKAGES.md).

It takes the starter pack's Ironwood faction (from a *built* starter pack, see below) and writes a renamed copy as a
`.zharmy` file: every definition, side, string label, model, texture and sound gets the package's tag. It exists so
the engine's loader can be tested before the real converter (tools/zharmy) does the same job for mod armies. It is
simple and specific to the starter pack on purpose; it is not the converter.

    python3 Content/StarterPack/build_pack.py <dir> --name starterpack
    python3 scripts/zharmy_testpkg/build_testpkg.py --pack <dir>/starterpack --tag IRB --id test.ironwood-irb -o irb.zharmy

`--tweak` makes a deliberately broken package for the negative tests (see TWEAKS). Standard library only
(the starter pack's own writers in Content/StarterPack/tools/spk are used for the skirmish script file).
"""

import argparse
import hashlib
import zlib
import json
import os
import re
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "Content", "StarterPack", "tools"))

from spk.datachunk import Dict  # noqa: E402
from spk.scripts import Script, action, condition, write_skirmish_file, THIS_TEAM  # noqa: E402

TWEAKS = {
    "redefine-fx": "tag FX and a copy of the base FXList FX_StarterMuzzle (a prefixed name that already exists)",
    "unprefixed": "an object without the tag prefix",
    "redefine-base": "a copy of the base armor IronwoodInfantryArmor (an unprefixed name that exists)",
    "shadow": "an asset file that the game data has already (use with --tag IW)",
    "badside": "an object on a side that is not one of the package's",
    "aidata-global": "AIData with a global setting",
    "badhash": "the content hash is wrong",
    "format2": "format 2",
    "noai-build": "ai: true but no SkirmishBuildList",
    "scb-foreign": "skirmish scripts for a player of the game data",
    "corrupt": "the zip is damaged (central directory)",
    "truncated": "the zip is cut off",
    "badvalue": "a value the INI reader cannot parse (the load fails halfway)",
    "badjson": "the manifest is not JSON",
    "notzip": "not a zip at all",
    "extrafile": "an entry outside Art/, Data/Audio/ and Army/",
    "evil-path": "an entry with '..'",
    "weaponblock-in-object": "a Weapon block in Object.ini",
}


# ------------------------------------------------------------------------------------------------ text helpers
def read(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


BLOCK_RE = re.compile(r"^([A-Za-z]+) (\S+)\s*(;.*)?$")


def blocks(text):
    """Top level blocks (column 0 header, column 0 End) of a starter INI file: [(keyword, name, text)]."""
    out = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = BLOCK_RE.match(lines[i])
        if m and not lines[i].startswith((" ", ";")):
            j = i + 1
            while j < len(lines) and lines[j].rstrip() != "End":
                j += 1
            out.append((m.group(1), m.group(2), "\n".join(lines[i:j + 1]) + "\n"))
            i = j + 1
        else:
            i += 1
    return out


def strings_table(text):
    """generals.str -> {label: text}"""
    table = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line or line.startswith("//"):
            i += 1
            continue
        label = line
        j = i + 1
        body = ""
        while j < len(lines) and lines[j].strip() != "END":
            body += lines[j] + "\n"
            j += 1
        body = body.strip()
        if len(body) >= 2 and body[0] == '"' and body[-1] == '"':
            body = body[1:-1]
        table[label] = body
        i = j + 1
    return table


# ------------------------------------------------------------------------------------------------ W3D renaming
def w3d_walk(data, rename_name, rename_texture, pos=0, end=None):
    """Rewrites the names inside a W3D chunk stream. Sizes are recomputed (a texture name may change length)."""
    end = len(data) if end is None else end
    out = bytearray()
    while pos < end:
        ctype, size = struct.unpack_from("<II", data, pos)
        container = bool(size & 0x80000000)
        size &= 0x7FFFFFFF
        payload = data[pos + 8:pos + 8 + size]
        if container:
            payload = w3d_walk(data, rename_name, rename_texture, pos + 8, pos + 8 + size)
        elif ctype == 0x1F:      # mesh header: name[16] at 8, container name[16] at 24
            payload = bytearray(payload)
            payload[24:40] = fixed(rename_name(cstr(payload[24:40])), 16)
            payload = bytes(payload)
        elif ctype == 0x101:     # hierarchy header: name[16] at 4
            payload = bytearray(payload)
            payload[4:20] = fixed(rename_name(cstr(payload[4:20])), 16)
            payload = bytes(payload)
        elif ctype == 0x701:     # hlod header: name[16] at 8, hierarchy name[16] at 24
            payload = bytearray(payload)
            payload[8:24] = fixed(rename_name(cstr(payload[8:24])), 16)
            payload[24:40] = fixed(rename_name(cstr(payload[24:40])), 16)
            payload = bytes(payload)
        elif ctype == 0x704:     # hlod sub object: bone index, name[32] = "<container>.<mesh>"
            payload = bytearray(payload)
            full = cstr(payload[4:36])
            container_name, _, mesh = full.partition(".")
            payload[4:36] = fixed("%s.%s" % (rename_name(container_name), mesh), 32)
            payload = bytes(payload)
        elif ctype == 0x32:      # texture name, NUL terminated
            payload = rename_texture(cstr(payload)).encode("ascii") + b"\0"
        flag = 0x80000000 if container else 0
        out += struct.pack("<II", ctype, len(payload) | flag) + payload
        pos += 8 + size
    return bytes(out)


def cstr(raw):
    return bytes(raw).split(b"\0", 1)[0].decode("ascii")


def fixed(text, n):
    raw = text.encode("ascii")
    if len(raw) >= n:
        raise ValueError("name %r does not fit %d bytes" % (text, n))
    return raw + b"\0" * (n - len(raw))


# ------------------------------------------------------------------------------------------------ the package
class Builder:
    def __init__(self, pack, tag, side_name, ai, requires, version):
        self.pack = pack
        self.tag = tag
        self.low = tag.lower()
        self.ai = ai
        self.requires = requires
        self.version = version
        self.side = "%s_%s" % (tag, side_name)
        self.template = "%s_FactionIronwood" % tag
        self.files = {}          # entry name -> bytes
        self.labels = {}         # new label -> text
        self.ini = {}            # file name -> text

    # -- reading the built starter pack
    def pack_ini(self, name):
        return read(os.path.join(self.pack, "data", "ini", name))

    def prefix(self, name):
        return "%s_%s" % (self.tag, name)

    def build(self):
        tag = self.tag
        object_blocks = [b for b in blocks(self.pack_ini("object.ini")) if b[1].startswith("Ironwood")]
        weapon_blocks = [b for b in blocks(self.pack_ini("weapon.ini")) if b[1].startswith("Ironwood")]
        loco_blocks = [b for b in blocks(self.pack_ini("locomotor.ini")) if b[1].startswith("Ironwood")]
        armor_blocks = [b for b in blocks(self.pack_ini("armor.ini")) if b[1].startswith("Ironwood")]
        set_blocks = [b for b in blocks(self.pack_ini("commandset.ini")) if b[1].startswith("Ironwood")]
        all_buttons = blocks(self.pack_ini("commandbutton.ini"))
        template_block = [b for b in blocks(self.pack_ini("playertemplate.ini")) if b[1] == "FactionIronwood"][0]

        chosen = object_blocks + weapon_blocks + loco_blocks + armor_blocks + set_blocks
        chosen_text = "\n".join(b[2] for b in chosen)

        # buttons used by the command sets and named for Ironwood are copied; shared ones (Stop, Guard, ...) stay references
        used = set(re.findall(r"=\s*(Command_\w+)", "\n".join(b[2] for b in set_blocks)))
        button_blocks = [b for b in all_buttons if b[1] in used and "Ironwood" in b[1]]
        chosen += button_blocks
        chosen_text += "\n".join(b[2] for b in button_blocks)

        # effects, particle systems and sounds those use
        fx_all = {b[1]: b for b in blocks(self.pack_ini("fxlist.ini"))}
        fx_names = sorted(set(re.findall(r"\bFX_Starter\w+", chosen_text)) & set(fx_all))
        fx_blocks = [fx_all[n] for n in fx_names]
        fx_text = "\n".join(b[2] for b in fx_blocks)
        ps_all = {b[1]: b for b in blocks(self.pack_ini("particlesystem.ini"))}
        ps_names = sorted(set(re.findall(r"Name = (Starter\w+)", fx_text)) & set(ps_all))
        ps_blocks = [ps_all[n] for n in ps_names]
        sound_all = {b[1]: b for b in blocks(self.pack_ini("soundeffects.ini"))}
        voice_names = sorted(set(re.findall(r"Voice\w+ = (Starter\w+)", chosen_text)) & set(sound_all))
        fxsound_names = sorted(set(re.findall(r"Name = (Starter\w+)", fx_text)) & set(sound_all) - set(ps_names))
        voice_blocks = [sound_all[n] for n in voice_names]
        fxsound_blocks = [sound_all[n] for n in fxsound_names]

        # ---- the rename map (definitions only)
        rename = {}
        for b in object_blocks + weapon_blocks + loco_blocks + armor_blocks + set_blocks + button_blocks:
            rename[b[1]] = self.prefix(b[1])
        for n in fx_names + ps_names + voice_names + fxsound_names:
            rename[n] = self.prefix(n)
        rename["FactionIronwood"] = self.template

        # models: iw_hq -> <TAG>_hq
        model_rename = {}
        for b in object_blocks:
            for m in re.findall(r"Model = (iw_\w+)", b[2]):
                model_rename[m] = "%s_%s" % (tag, m[3:])
        palette_new = "%s_pal.tga" % tag

        label_re = re.compile(r"\b((?:OBJECT|CONTROLBAR|INI|TOOLTIP|SIDE):\w+)")
        id_re = re.compile(r"\b(" + "|".join(re.escape(n) for n in sorted(rename, key=len, reverse=True)) + r")\b")
        model_re = re.compile(r"Model = (iw_\w+)")
        table = strings_table(read(os.path.join(self.pack, "data", "generals.str")))

        def transform(text):
            # labels first (they contain definition names), then names, then the rest
            labels = []

            def label_sub(m):
                labels.append(m.group(1))
                return "\x00%d\x00" % (len(labels) - 1)

            text = label_re.sub(label_sub, text)
            text = model_re.sub(lambda m: "Model = " + model_rename[m.group(1)], text)
            text = id_re.sub(lambda m: rename[m.group(1)], text)
            text = re.sub(r"^(\s*(?:Base)?Side = )Ironwood\s*$", lambda m: m.group(1) + self.side, text, flags=re.M)
            # shared palette texture is renamed in the models; the INI does not name it

            def restore(m):
                old = labels[int(m.group(1))]
                if old == "SIDE:Ironwood":
                    new = "SIDE:" + self.side
                else:
                    new = "%s:%s" % (tag, old)
                self.labels[new] = table.get(old, old)
                return new

            return re.sub(r"\x00(\d+)\x00", restore, text)

        def join(bl):
            return "\n".join(transform(b[2]) for b in bl)

        header = "; Test package built from the starter pack's Ironwood faction by scripts/zharmy_testpkg. Not the converter.\n\n"
        ini = {}
        ini["object.ini"] = header + join(object_blocks)
        ini["weapon.ini"] = header + join(weapon_blocks)
        ini["locomotor.ini"] = header + join(loco_blocks)
        ini["armor.ini"] = header + join(armor_blocks)
        ini["commandset.ini"] = header + join(set_blocks)
        ini["commandbutton.ini"] = header + join(button_blocks)
        ini["fxlist.ini"] = header + join(fx_blocks)
        ini["particlesystem.ini"] = header + join(ps_blocks)
        ini["voice.ini"] = header + join(voice_blocks)
        ini["soundeffects.ini"] = header + join(fxsound_blocks)

        template_text = transform(template_block[2])
        ini["playertemplate.ini"] = header + template_text

        # a cameo image of our own (same texture as the game's, own name), used by the worker's build button
        worker_button = "Command_TrainIronwoodWorker"
        image_name = self.prefix("WorkerCameo")
        ini["mappedimages.ini"] = header + (
            "MappedImage %s\n  Texture = sp_ui.tga\n  TextureWidth = 512\n  TextureHeight = 512\n"
            "  Coords = Left:163 Top:178 Right:227 Bottom:226\n  Status = NONE\nEnd\n" % image_name)
        ini["commandbutton.ini"] = ini["commandbutton.ini"].replace(
            "ButtonImage = SP_Worker", "ButtonImage = " + image_name)

        # an upgrade and a science nobody uses, to exercise those block types
        ini["upgrade.ini"] = header + (
            "Upgrade %s\n  DisplayName = %s:UPGRADE:Test\n  Type = OBJECT\n  BuildTime = 10.0\n  BuildCost = 100\n"
            "  ButtonImage = SP_HQ\nEnd\n" % (self.prefix("Upgrade_Test"), tag))
        self.labels["%s:UPGRADE:Test" % tag] = "Test upgrade"
        ini["science.ini"] = header + (
            "Science %s\n  PrerequisiteSciences = None\n  SciencePurchasePointCost = 1\n  IsGrantable = No\n"
            "  DisplayName = %s:SCIENCE:Test\n  Description = %s:SCIENCE:TestHelp\nEnd\n" % (self.prefix("SCIENCE_Test"), tag, tag))
        self.labels["%s:SCIENCE:Test" % tag] = "Test science"
        self.labels["%s:SCIENCE:TestHelp" % tag] = "A science added by a test package."

        # AIData: the side's resource settings and build list, renamed
        ai_text = self.pack_ini("aidata.ini")
        side_info = re.search(r"^  SideInfo Ironwood\n.*?^  End\n", ai_text, re.S | re.M).group(0)
        build_list = re.search(r"^  SkirmishBuildList Ironwood\n.*?^  End\n(?=End|\Z)", ai_text, re.S | re.M).group(0)
        ai_block = "AIData\n" + transform(side_info).replace("SideInfo Ironwood", "SideInfo " + self.side)
        build = transform(build_list).replace("SkirmishBuildList Ironwood", "SkirmishBuildList " + self.side)
        if self.ai:
            ai_block += build
        ai_block += "End\n"
        ai_block = ai_block.replace("SideInfo Ironwood", "SideInfo " + self.side)
        ini["aidata.ini"] = header + ai_block

        # audio files used by the copied events
        sound_names = {}
        for b in voice_blocks + fxsound_blocks:
            m = re.search(r"Sounds = (\w+)", b[2])
            sound_names[m.group(1)] = "%s_%s" % (self.low, m.group(1))
        for old, new in sound_names.items():
            self.files["Data/Audio/Sounds/%s.wav" % new] = read_bytes(os.path.join(self.pack, "data", "audio", "sounds", old + ".wav"))
        for k in ("voice.ini", "soundeffects.ini"):
            ini[k] = re.sub(r"Sounds = (\w+)", lambda m: "Sounds = " + sound_names[m.group(1)], ini[k])

        # models and the palette texture
        for old, new in model_rename.items():
            data = read_bytes(os.path.join(self.pack, "art", "w3d", old + ".w3d"))
            data = w3d_walk(data,
                            lambda n: model_rename.get(n, n),
                            lambda t: palette_new if t.lower() == "sp_iw.tga" else t)
            self.files["Art/W3D/%s.w3d" % new] = data
        self.files["Art/Textures/" + palette_new] = read_bytes(os.path.join(self.pack, "art", "textures", "sp_iw.tga"))

        # strings
        self.display_name = "%s Ironwood" % tag      # short: the starter's faction list is narrow
        self.labels["SIDE:" + self.side] = self.display_name
        self.labels["%s:INI:FactionIronwood" % tag] = self.display_name
        out = "// Test package strings.\n\n"
        for k in sorted(self.labels):
            out += '%s\n"%s"\nEND\n\n' % (k, self.labels[k].replace('"', '\\"'))
        self.files["Army/Strings.str"] = out.encode("utf-8")

        # skirmish scripts
        self.files["Army/Scripts/Skirmish.scb"] = self.skirmish_scripts()

        self.ini = ini
        for name, text in ini.items():
            self.files["Army/INI/" + name] = text.encode("utf-8")
        self.files["README.txt"] = ("Test package for the engine side of army packages, made from the starter pack's Ironwood "
                                    "faction (GPL-3.0-or-later).\n").encode("ascii")

    def skirmish_scripts(self, owner=None):
        tag = self.tag
        ai_side = owner or ("Skirmish" + self.side)
        teams_spec = [
            ("TeamRiflemen", [("IronwoodRifleman", 3, 4)], 1, 3, "BuildRiflemen"),
            ("TeamRockets", [("IronwoodRocketeer", 2, 3), ("IronwoodRifleman", 1, 2)], 2, 2, "BuildRockets"),
            ("TeamArmor", [("IronwoodTank", 2, 3), ("IronwoodScout", 1, 1)], 3, 3, "BuildArmor"),
        ]
        scripts = []
        for _name, _units, _prio, _max, cond in teams_spec:
            scripts.append(Script("%s_%s" % (tag, cond), conditions=[[condition("CONDITION_TRUE")]], actions=[], one_shot=False,
                                  comment="Production condition for a team."))
        attack = "%s_AttackWave" % tag
        scripts.append(Script(attack, conditions=[[condition("CONDITION_TRUE")]], actions=[action("TEAM_HUNT", THIS_TEAM)],
                              subroutine=True, one_shot=False, comment="A finished team hunts the nearest enemy."))
        teams = [Dict(teamName="team" + ai_side, teamOwner=ai_side, teamIsSingleton=True)]
        for name, units, priority, max_instances, cond in teams_spec:
            d = Dict()
            d.set("teamName", "%s_%s" % (tag, name))
            d.set("teamOwner", ai_side)
            d.set("teamIsSingleton", False)
            for i, (unit, lo, hi) in enumerate(units, 1):
                d.set("teamUnitType%d" % i, "%s_%s" % (tag, unit))
                d.set("teamUnitMinCount%d" % i, lo)
                d.set("teamUnitMaxCount%d" % i, hi)
            d.set("teamMaxInstances", max_instances)
            d.set("teamProductionPriority", priority)
            d.set("teamProductionPrioritySuccessIncrease", 1)
            d.set("teamProductionPriorityFailureDecrease", 1)
            d.set("teamProductionCondition", "%s_%s" % (tag, cond))
            d.set("teamOnCreateScript", attack)
            d.set("teamAutoReinforce", False)
            d.set("teamIsAIRecruitable", False)
            d.set("teamExecutesActionsOnCreate", False)
            teams.append(d)
        return write_skirmish_file([(ai_side, scripts)], teams)

    # -- tweaks for the negative tests
    def tweak(self, name):
        tag = self.tag
        if name == "redefine-fx":
            fx = [b for b in blocks(self.pack_ini("fxlist.ini")) if b[1] == "FX_StarterMuzzle"][0]
            self.files["Army/INI/fxlist.ini"] = (self.files.get("Army/INI/fxlist.ini", b"").decode() + "\n" + fx[2]).encode()
        elif name == "unprefixed":
            self.files["Army/INI/object.ini"] += b"\nObject LooseObject\n  Scale = 1.0\nEnd\n"
        elif name == "redefine-base":
            blk = [b for b in blocks(self.pack_ini("armor.ini")) if b[1] == "IronwoodInfantryArmor"][0]
            self.files["Army/INI/armor.ini"] += ("\n" + blk[2]).encode()
        elif name == "shadow":
            self.files["Art/W3D/iw_factory.w3d"] = read_bytes(os.path.join(self.pack, "art", "w3d", "iw_factory.w3d"))
        elif name == "badside":
            self.files["Army/INI/object.ini"] += ("\nObject %s_Intruder\n  Side = Ironwood\n  Scale = 1.0\nEnd\n" % tag).encode()
        elif name == "aidata-global":
            self.files["Army/INI/aidata.ini"] = self.files["Army/INI/aidata.ini"].replace(b"AIData\n", b"AIData\n  StructureSeconds = 1.0\n", 1)
        elif name == "noai-build":
            self.files["Army/INI/aidata.ini"] = re.sub(rb"  SkirmishBuildList .*?\n  End\n(?=End)", b"",
                                                       self.files["Army/INI/aidata.ini"], flags=re.S)
        elif name == "scb-foreign":
            self.files["Army/Scripts/Skirmish.scb"] = self.skirmish_scripts(owner="SkirmishIronwood")
        elif name == "badvalue":
            self.files["Army/INI/weapon.ini"] = self.files["Army/INI/weapon.ini"].replace(b"PrimaryDamage = 6.0", b"PrimaryDamage = lots", 1)
        elif name == "extrafile":
            self.files["Data/INI/Object.ini"] = b"; sneaky\n"
        elif name == "weaponblock-in-object":
            self.files["Army/INI/object.ini"] += ("\nWeapon %s_Sneaky\n  PrimaryDamage = 1.0\nEnd\n" % tag).encode()
        elif name in ("badhash", "format2", "badjson", "corrupt", "truncated", "notzip", "evil-path"):
            pass  # applied while writing
        else:
            raise SystemExit("unknown tweak " + name)

    def manifest(self, pkg_id, tweaks):
        digest = hashlib.sha256()
        for name in sorted(n for n in self.files if n.lower() != "manifest.json" and not n.endswith("/")):
            data = self.files[name]
            digest.update(name.lower().encode("utf-8") + b"\0" + struct.pack("<Q", len(data)) + struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF))
        h = digest.hexdigest()
        if "badhash" in tweaks:
            h = "0" * 63 + "1"
        return {
            "format": 2 if "format2" in tweaks else 1,
            "id": pkg_id,
            "tag": self.tag,
            "name": "Ironwood test copy %s" % self.tag,
            "version": self.version,
            "description": "The starter pack's Ironwood Compact, renamed with the tag %s." % self.tag,
            "authors": ["scripts/zharmy_testpkg"],
            "license": "GPL-3.0-or-later",
            "source": {"mod": "starter pack", "modVersion": "test"},
            "requires": self.requires,
            "factions": [{"playerTemplate": self.template, "side": self.side, "displayName": self.display_name, "ai": self.ai}],
            "contentHash": "sha256:" + h,
            "converter": {"tool": "zharmy_testpkg", "version": "0", "warnings": []},
        }

    def write(self, out_path, pkg_id, tweaks):
        files = dict(self.files)
        if "evil-path" in tweaks:
            files["Art/W3D/../../escape.txt"] = b"x"
        manifest = self.manifest(pkg_id, tweaks)
        manifest_bytes = json.dumps(manifest, indent=2).encode("utf-8")
        if "badjson" in tweaks:
            manifest_bytes = manifest_bytes[:-5]
        with zipfile.ZipFile(out_path, "w") as z:
            def add(name, data, method):
                info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = method
                info.external_attr = 0o644 << 16
                z.writestr(info, data)
            add("manifest.json", manifest_bytes, zipfile.ZIP_DEFLATED)
            for name in sorted(files):
                method = zipfile.ZIP_STORED if name.lower().endswith((".wav", ".tga")) else zipfile.ZIP_DEFLATED
                add(name, files[name], method)
        if "corrupt" in tweaks:
            raw = bytearray(read_bytes(out_path))
            eocd = raw.rfind(b"PK\x05\x06")
            cd_off = struct.unpack_from("<I", raw, eocd + 16)[0]
            raw[cd_off + 4] ^= 0xFF          # break the central directory signature of the first entry's neighbour
            raw[cd_off] = 0x00
            with open(out_path, "wb") as f:
                f.write(raw)
        if "truncated" in tweaks:
            raw = read_bytes(out_path)
            with open(out_path, "wb") as f:
                f.write(raw[:len(raw) // 2])
        if "notzip" in tweaks:
            with open(out_path, "wb") as f:
                f.write(b"this is not a zip archive " * 40)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pack", required=True, help="a built starter pack (the folder with data/, art/, ...)")
    ap.add_argument("--tag", default="IRB")
    ap.add_argument("--id", default=None)
    ap.add_argument("--side-name", default="Ironwood")
    ap.add_argument("--no-ai", action="store_true", help="ai: false in the manifest, no AI data")
    ap.add_argument("--requires", default="starter", help="comma separated rulesets, '' for any")
    ap.add_argument("--version", default="1.0.0")
    ap.add_argument("--tweak", action="append", default=[], choices=sorted(TWEAKS), help="; ".join("%s: %s" % kv for kv in sorted(TWEAKS.items())))
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    pkg_id = args.id or ("test.ironwood-" + args.tag.lower())
    requires = [r for r in args.requires.split(",") if r]
    b = Builder(args.pack, args.tag, args.side_name, not args.no_ai, requires, args.version)
    b.build()
    for t in args.tweak:
        b.tweak(t)
    b.write(args.out, pkg_id, args.tweak)
    print("wrote %s (%d entries)" % (args.out, len(b.files) + 1))


if __name__ == "__main__":
    main()
