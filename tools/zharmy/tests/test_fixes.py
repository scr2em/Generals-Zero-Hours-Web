"""Regression tests for the problems found in the first real runs: mod archive selection, folder layouts, string
language, dangling references, asset search paths, INI parsing, output volume. All data is synthetic (fixtures.py and
the builders below)."""

import io
import json
import os
import shutil
import struct
import tempfile
import unittest
import zlib
from contextlib import redirect_stderr, redirect_stdout

from . import _path  # noqa: F401
from zharmy import ini as inimod
from zharmy import layout, package, scb as scbmod, schema_data, search, strings as stringsmod
from zharmy import validate as validatemod
from zharmy.bigfile import BigWriter
from zharmy.cli import main
from zharmy.convert import (CannotConvert, ConvertError, Context, Options, Unplayable, convert, convert_all,
                            format_report, format_summary)
from zharmy.gamedata import GameData, format_ini_problems
from zharmy.inspect_mod import format_factions, inspect_factions
from zharmy.vfs import Vfs
from . import fixtures
from .fixtures import w3d_anim, w3d_model, w3d_skeleton


def run_cli(*args):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main(list(args))
    return code, out.getvalue(), err.getvalue()


def write_big(path, files):
    big = BigWriter()
    for k, v in files.items():
        big.add(k, v if isinstance(v, bytes) else v.encode("latin-1"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    big.write(path)


# a small mod ("Dg") whose armies refer to things the mod itself does not have, plus two broken armies
DG_INI = {
    "Data\\INI\\Object\\Dg.ini": """
Object DgHQ
  Side = DgSide
  DisplayName = OBJECT:DgHQ
  ButtonImage = DG_NOIMAGE
  VoiceSelect = DgNoVoice
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = dg_nomodel
      Animation = DG_SKL.DG_NOANIM
    End
  End
  KindOf = STRUCTURE SELECTABLE COMMANDCENTER
  Body = StructureBody ModuleTag_Body
    MaxHealth = 100.0
  End
  Behavior = FXListDie ModuleTag_Die
    DeathFX = FX_DgNone
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY DgGun
  End
End

Object DgDozer
  Side = DgSide
  DisplayName = OBJECT:DgDozer
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = dg_ok
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 50.0
  End
End

Object DgBadHQ
  Side = DgBad
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = dg_ok
    End
  End
  Prerequisites
    Science = SCIENCE_DgGhost
  End
End
""",
    "Data\\INI\\Weapon\\Dg.ini": """
Weapon DgGun
  PrimaryDamage = 1.0
  ProjectileObject = DgNoProj
  FireFX = FX_Dg
End
""",
    "Data\\INI\\FXList\\Dg.ini": """
FXList FX_Dg
  ParticleSystem
    Name = DgNoParticles
  End
End
""",
    "Data\\INI\\PlayerTemplate\\Dg.ini": """
PlayerTemplate FactionDg
  Side = DgSide
  BaseSide = DgSide
  PlayableSide = Yes
  DisplayName = INI:DgSide
  StartingBuilding = DgHQ
  StartingUnit0 = DgDozer
End

PlayerTemplate FactionDgNoStart
  Side = DgSide2
  BaseSide = DgSide2
  PlayableSide = Yes
  DisplayName = INI:DgSide2
  StartingBuilding = DgGhostHQ
  StartingUnit0 = DgDozer
End

PlayerTemplate FactionDgBad
  Side = DgBad
  BaseSide = DgBad
  PlayableSide = Yes
  DisplayName = INI:DgBad
  StartingBuilding = DgBadHQ
End
""",
}


def dg_strings():
    s = dict(fixtures.base_strings())
    s.update({"INI:DgSide": "Dg Army", "INI:DgSide2": "Dg Two", "INI:DgBad": "Dg Bad", "OBJECT:DgDozer": "Dozer"})
    return s


def build_dg_mod(folder, strings=None, name="!DgMod.big"):
    files = dict(DG_INI)
    files["Data\\English\\generals.csf"] = stringsmod.build_csf(strings or dg_strings())
    files["Art\\W3D\\DG_OK.W3D"] = w3d_model("DG_OK", "dg_missing_tex.tga")
    write_big(os.path.join(folder, name), files)


class Tmp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="zharmy-fixes-")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def path(self, *parts):
        return os.path.join(self.tmp, *parts)


# ---- 1. selecting the mod's archives ------------------------------------------------------------------------------
class RetailNames(unittest.TestCase):
    def test_retail_names_and_language_variants(self):
        for name in layout.RETAIL_ZERO_HOUR + layout.RETAIL_GENERALS:
            self.assertTrue(layout.is_retail_archive(name), name)
        for name in ("AudioGermanZH.big", "audiogermanzh.BIG", "W3DFrenchZH.big", "GermanZH.big", "SpeechKoreanZH.big",
                     "AudioChinese.big", "Data/INI/INIZH.big", "TexturesZH.big"):
            self.assertTrue(layout.is_retail_archive(name), name)
        for name in ("!00egypatch.big", "00lsf0118.big", "Contra009Final.big", "!Contra009_Patch.big", "zModArt.big",
                     "INIZH2.big", "TexturesZHPlus.big", "Patch2.big", "readme.txt", "ModINI.big"):
            self.assertFalse(layout.is_retail_archive(name), name)


class ModArchiveSelection(Tmp):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.installed = cls.path_of(cls, "installed")
        fixtures.build_base(cls.installed)
        fixtures.build_mod_archives(cls.installed)
        cls.retail = cls.path_of(cls, "retail")
        fixtures.build_base(cls.retail)

    def path_of(self, *parts):
        return os.path.join(self.tmp, *parts)

    def test_pattern_matching_nothing_is_an_error_listing_the_archives(self):
        with self.assertRaises(ConvertError) as cm:
            inspect_factions([self.installed], [self.installed], mod_archives=["!Contra*.big", "Contra*.big"])
        text = str(cm.exception)
        self.assertIn("matches no archive", text)
        for name in ("INIZH.big", "!ModMain.big", "zModArt.big"):
            self.assertIn(name, text)
        self.assertIn("NOT a retail name", text)
        code, out, err = run_cli("inspect", self.installed, "--base", self.installed, "--mod-archives",
                                 "!Contra*.big", "-q")
        self.assertEqual(code, 2)
        self.assertIn("matches no archive", err)
        with self.assertRaises(ConvertError):
            convert_all([self.installed], [self.installed], self.path_of("o1"), "X", "zerohour",
                        mod_archives=["Contra*.big"])

    def test_one_good_and_one_bad_pattern_still_fails(self):
        with self.assertRaises(ConvertError):
            Context([self.installed], [self.installed], mod_archives=["!ModMain.big", "nothing*.big"])

    def test_same_folder_nothing_selected_is_an_error(self):
        # only retail names: auto (the default) finds nothing, and the tool says what to do
        with self.assertRaises(ConvertError) as cm:
            Context([self.retail], [self.retail])
        self.assertIn("--mod-archives", str(cm.exception))
        self.assertIn("INIZH.big", str(cm.exception))
        code, out, err = run_cli("inspect", self.retail, "--base", self.retail, "-q")
        self.assertEqual(code, 2)
        self.assertIn("--mod-archives", err)

    def test_explicit_empty_selection_is_allowed(self):
        ctx = Context([self.retail], [self.retail], mod_archives=[])
        self.assertTrue(any("whole folder is the ruleset" in n for n in ctx.notes))

    def test_auto_is_the_default_and_lists_what_it_picked(self):
        ctx = Context([self.installed], [self.installed])
        picked = [rel for _f, rel in ctx.selection.picked]
        self.assertEqual(sorted(picked, key=str.lower), ["!ModMain.big", "zModArt.big"])
        self.assertTrue(any(n.startswith("Mod archives (auto") for n in ctx.notes))
        self.assertTrue(any(n.strip() == "!ModMain.big" for n in ctx.notes))
        # the ruleset has none of the mod's definitions, the mod as played has them
        self.assertIsNone(ctx.base.get("Object", "MAlphaHQ"))
        self.assertIsNotNone(ctx.mod.get("Object", "MAlphaHQ"))
        # explicit "auto" gives the same
        ctx2 = Context([self.installed], [self.installed], mod_archives=["auto"])
        self.assertEqual(ctx2.selection.picked, ctx.selection.picked)

    def test_archives_command_shows_the_automatic_selection(self):
        code, out, err = run_cli("archives", self.installed)
        self.assertEqual(code, 0, err)
        lines = {l.split()[1]: l for l in out.splitlines() if ".big" in l}
        self.assertIn("MOD", lines["!ModMain.big"])
        self.assertIn("MOD", lines["zModArt.big"])
        self.assertIn("ruleset", lines["INIZH.big"])
        self.assertIn("retail name", lines["INIZH.big"])
        code, out, err = run_cli("archives", self.installed, "--mod-archives", "zMod*.big")
        self.assertIn("ruleset", [l for l in out.splitlines() if "!ModMain" in l][0])


# ---- 2. folder layout ---------------------------------------------------------------------------------------------
def build_two_installs(root):
    """A game folder with the Zero Hour install and the Generals install as sub folders (different names than the real
    ones on purpose)."""
    zh = os.path.join(root, "zero hour")
    gen = os.path.join(root, "generals")
    # Zero Hour: retail archives, a mod archive, a loose file
    write_big(os.path.join(zh, "INIZH.big"), {
        "Data\\Marker.txt": "zero hour", "Data\\OnlyZH.txt": "zh only",
        "Data\\INI\\Object.ini": "Object TstA\nEnd\n"})
    write_big(os.path.join(zh, "!ModPatch.big"), {"Data\\Marker2.txt": "mod"})
    write_big(os.path.join(zh, "EnglishZH.big"), {"Data\\English\\generals.csf": stringsmod.build_csf({"A:B": "c"})})
    fixtures.wr(os.path.join(zh, "loose.txt"), b"loose zh")
    # Generals: the base game's archives
    write_big(os.path.join(gen, "INI.big"), {
        "Data\\Marker.txt": "generals", "Data\\OnlyGenerals.txt": "gen only",
        "Data\\Scripts\\SkirmishScripts.scb": fixtures.make_scb({"SkirmishTstBase": [("S1", [])]})})
    write_big(os.path.join(gen, "Textures.big"), {"Art\\Textures\\gen_tex.tga": b"tga"})
    fixtures.wr(os.path.join(gen, "loose.txt"), b"loose generals")


class FolderLayout(Tmp):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.root = os.path.join(cls.tmp, "game")
        build_two_installs(cls.root)

    def test_detects_two_installs_zero_hour_first(self):
        lay = layout.detect_layout(self.root)
        self.assertTrue(lay.multi)
        self.assertEqual([(i.rel, i.kind) for i in lay.installs], [("zero hour", "zerohour"), ("generals", "generals")])
        text = "\n".join(lay.lines())
        self.assertIn("2 game installs", text)
        self.assertLess(text.index("zero hour"), text.index("generals"))
        single = layout.detect_layout(os.path.join(self.root, "zero hour"))
        self.assertFalse(single.multi)

    def test_zero_hour_archives_win_over_generals_and_loose_files_of_generals_are_invisible(self):
        vfs = Vfs()
        layout.add_game_tree(vfs, self.root, overwrite=False)
        self.assertEqual(vfs.read("Data/Marker.txt"), b"zero hour")       # both have it: Zero Hour first
        self.assertEqual(vfs.read("Data/OnlyGenerals.txt"), b"gen only")  # only the base game has it
        self.assertEqual(vfs.read("Data/OnlyZH.txt"), b"zh only")
        self.assertEqual(vfs.read("loose.txt"), b"loose zh")              # the engine's working directory
        self.assertTrue(vfs.exists("Art/Textures/gen_tex.tga"))
        # list order: Zero Hour archives first
        rels = [a.rel for a in layout.list_archives(self.root)]
        self.assertEqual(rels[0].split("/")[0], "zero hour")
        self.assertEqual(rels[-1].split("/")[0], "generals")

    def test_mod_archive_in_the_zero_hour_folder_wins_and_is_picked_by_auto(self):
        ctx = Context([self.root], [self.root], requires="zerohour")
        picked = [rel for _f, rel in ctx.selection.picked]
        self.assertEqual(picked, ["zero hour/!ModPatch.big"])
        self.assertTrue(any(n.startswith("Folder layout") for n in ctx.notes))
        self.assertTrue(ctx.mod_vfs.exists("Data/Marker2.txt"))
        self.assertFalse(ctx.base_vfs.exists("Data/Marker2.txt"))
        self.assertEqual(ctx.base_vfs.read("Data/Marker.txt"), b"zero hour")

    def test_output_says_so(self):
        code, out, err = run_cli("archives", self.root)
        self.assertEqual(code, 0, err)
        self.assertIn("2 game installs", out)


# ---- 3. SkirmishScripts.scb ----------------------------------------------------------------------------------------
class SkirmishScriptsLookup(Tmp):
    def test_found_in_the_base_game_sub_folder(self):
        root = self.path("g1")
        build_two_installs(root)
        ctx = Context([root], [root])
        scb, problem = ctx.skirmish_scb()
        self.assertIsNone(problem)
        self.assertEqual([n for n, _d in scb.players], ["SkirmishTstBase"])
        self.assertTrue(any("SkirmishScripts.scb read from" in n and "INI.big" in n for n in ctx.notes))

    def test_loose_file_wins_and_case_does_not_matter(self):
        root = self.path("g2")
        build_two_installs(root)
        loose = scbmod.write_scb(scbmod.read_scb(fixtures.make_scb({"SkirmishLoose": [("L", [])]})))
        os.makedirs(os.path.join(root, "zero hour", "DATA", "scripts"))
        fixtures.wr(os.path.join(root, "zero hour", "DATA", "scripts", "SKIRMISHSCRIPTS.SCB"), loose)
        scb, problem = Context([root], [root]).skirmish_scb()
        self.assertEqual([n for n, _d in scb.players], ["SkirmishLoose"])

    def test_zlib_compressed_file_is_read_like_the_engine_does(self):
        raw = fixtures.make_scb({"SkirmishZ": [("Z", [])]})
        packed = b"ZL9\0" + struct.pack("<i", len(raw)) + zlib.compress(raw)
        self.assertEqual([n for n, _d in scbmod.read_scb(packed).players], ["SkirmishZ"])
        with self.assertRaises(scbmod.ScbError):
            scbmod.read_scb(b"EAR\0" + struct.pack("<i", 10) + b"xxxxxxxxxx")

    def test_absent_file_message_says_where_it_looked(self):
        root = self.path("g3")
        write_big(os.path.join(root, "INIZH.big"), {"Data\\INI\\Object.ini": "Object A\nEnd\n",
                                                    "Data\\Scripts\\SkirmishScript.scb": b"x"})
        write_big(os.path.join(root, "!Mod.big"), {"Data\\Marker.txt": "m"})
        ctx = Context([root], [root])
        scb, problem = ctx.skirmish_scb()
        self.assertIsNone(scb)
        self.assertIn("looked like the engine does", problem)
        self.assertIn("archives", problem)
        self.assertIn(root, problem)
        self.assertIn("data/scripts/skirmishscript.scb", problem)           # a near miss is shown
        self.assertIn("zharmy find", problem)


# ---- 4. string table language ---------------------------------------------------------------------------------------
class StringLanguage(Tmp):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.game = cls.path_of(cls, "game")
        fixtures.build_base(cls.game)                                    # retail: English table
        cn = dg_strings()
        cn.update({"INI:DgSide": "灵蛇军团", "OBJECT:DgHQ": "总部"})
        files = dict(DG_INI)
        files["Data\\Chinese\\generals.csf"] = stringsmod.build_csf(cn)
        files["Art\\W3D\\DG_OK.W3D"] = w3d_model("DG_OK", "tst_main.tga")
        write_big(os.path.join(cls.game, "!CnMod.big"), files)

    def path_of(self, *parts):
        return os.path.join(self.tmp, *parts)

    def test_mod_language_is_detected(self):
        ctx = Context([self.game], [self.game])
        self.assertEqual(ctx.language_name, "chinese")
        self.assertIn("only a chinese string table", ctx.language_why)
        self.assertTrue(any(n.startswith("String tables: chinese") for n in ctx.notes))
        self.assertEqual(ctx.mod_strings.get("INI:DgSide"), "灵蛇军团")

    def test_language_option_overrides(self):
        ctx = Context([self.game], [self.game], language="english")
        self.assertEqual(ctx.language_name, "english")
        self.assertIsNone(ctx.mod_strings.get("INI:DgSide"))

    def test_inspect_and_report_say_which_language(self):
        res = inspect_factions([self.game], [self.game])
        self.assertEqual(res["language"], "chinese")
        by = {f["playerTemplate"]: f for f in res["factions"]}
        self.assertEqual(by["FactionDg"]["displayName"], "灵蛇军团")
        self.assertIn("String tables: chinese", format_factions(res))
        out = self.path_of("cn.zharmy")
        c, rep = convert([self.game], [self.game], Options(tag="CNX", faction="FactionDg"), out)
        self.assertEqual(rep["language"]["name"], "chinese")
        self.assertEqual(package.Package(out).manifest()["factions"][0]["displayName"], "灵蛇军团")
        self.assertNotIn("OBJECT:DgHQ", " ".join(rep["strings"]["missing"]))

    def test_english_mod_still_english(self):
        installed = self.path_of("en")
        fixtures.build_base(installed)
        fixtures.build_mod_archives(installed)
        self.assertEqual(Context([installed], [installed]).language_name, "english")


# ---- 5/6. dangling references, unplayable and unconvertible factions -------------------------------------------------
class DanglingReferences(Tmp):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.retail = os.path.join(cls.tmp, "retail")
        fixtures.build_base(cls.retail)
        cls.mod = os.path.join(cls.tmp, "mod")
        build_dg_mod(cls.mod)
        cls.out = os.path.join(cls.tmp, "out")
        cls.rows, cls.ctx = convert_all([cls.mod], [cls.retail], cls.out, "DG", "zerohour")
        cls.by = {r["faction"]: r for r in cls.rows}

    def test_rows(self):
        self.assertIn("report", self.by["FactionDg"])
        self.assertIn("skipped", self.by["FactionDgNoStart"])
        self.assertIn("DgGhostHQ", self.by["FactionDgNoStart"]["skipped"])
        self.assertIn("cannot", self.by["FactionDgBad"])
        self.assertTrue(self.by["FactionDgBad"]["cannot"].startswith("cannot be converted"))
        self.assertIn("SCIENCE_DgGhost", self.by["FactionDgBad"]["cannot"])

    def test_no_package_for_unplayable_or_unconvertible(self):
        files = sorted(os.listdir(self.out))
        self.assertEqual(len(files), 2, files)                          # FactionTstBase (retail) and FactionDg
        self.assertTrue(any(f.endswith("_dg.zharmy") for f in files), files)
        self.assertFalse(any("nostart" in f.lower() or "bad" in f.lower() for f in files), files)
        text = format_summary(self.rows)
        self.assertIn("SKIPPED (no package written)", text)
        self.assertIn("cannot be converted", text)

    def test_single_convert_raises_the_same_reasons(self):
        with self.assertRaises(Unplayable):
            convert([self.mod], [self.retail], Options(tag="DGX", faction="FactionDgNoStart"),
                    os.path.join(self.tmp, "x.zharmy"))
        with self.assertRaises(CannotConvert):
            convert([self.mod], [self.retail], Options(tag="DGY", faction="FactionDgBad"),
                    os.path.join(self.tmp, "y.zharmy"))
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "y.zharmy")))

    def test_dangling_references_are_warnings_counted_separately(self):
        row = self.by["FactionDg"]
        rep = row["report"]
        kinds = {d["what"] for d in rep["danglingReferences"]}
        for expected in ("Object", "Audio", "FXList", "MappedImage", "ParticleSystem", "model", "texture",
                         "string label"):
            self.assertIn(expected, kinds)
        self.assertGreater(row["dangling"], 6)
        self.assertEqual(row["warnings"] + row["dangling"], len(rep["warnings"]))
        names = {d["name"] for d in rep["danglingReferences"]}
        self.assertIn("DgNoProj", names)
        self.assertIn("OBJECT:DgHQ", names)
        self.assertTrue(any(k.startswith("dangling ") for k in rep["warningsByKind"]))
        text = format_report(rep)
        self.assertIn("Pre-existing dangling references", text)

    def test_valid_with_the_mod_known_invalid_without(self):
        pkg = self.by["FactionDg"]["file"]
        res = validatemod.validate(pkg, [self.retail], mod_paths=[self.mod])
        self.assertTrue(res.ok, res.errors)
        self.assertGreater(len(res.dangling), 5)
        self.assertIn("pre-existing dangling", validatemod.format_result(res))
        strict = validatemod.validate(pkg, [self.retail])
        self.assertFalse(strict.ok)
        self.assertTrue(any("neither the package nor the ruleset" in m for _r, m in strict.errors))
        self.assertTrue(any("mod as played is not known" in n for n in strict.notes))
        # the converter's own context gives the same verdict as --mod
        res2 = validatemod.validate(pkg, [self.retail], world=self.ctx)
        self.assertTrue(res2.ok, res2.errors)

    def test_cli_validate_with_mod_option(self):
        pkg = self.by["FactionDg"]["file"]
        code, out, err = run_cli("validate", pkg, "--base", self.retail, "--mod", self.mod)
        self.assertEqual(code, 0, out + err)
        self.assertIn("pre-existing dangling", out)
        code, out, err = run_cli("validate", pkg, "--base", self.retail)
        self.assertEqual(code, 1)
        self.assertIn("mod as played is not known", out)

    def test_something_the_mod_has_but_the_package_lost_stays_an_error(self):
        # a reference to a ruleset-less object that the mod DOES define is not tolerated
        pkg_path = self.by["FactionDg"]["file"]
        edited = os.path.join(self.tmp, "edited.zharmy")
        import zipfile
        with zipfile.ZipFile(pkg_path) as zin:
            names = zin.namelist()
            data = {n: zin.read(n) for n in names}
        # DgDozer exists in the mod but not in the ruleset or the package
        data["army/ini/object.ini"] += b"\nObject DG_Extra\n  Side = DG_DgSide\n  Draw = W3DModelDraw ModuleTag_D\n" \
                                       b"    DefaultConditionState\n      Model = dg_ok\n    End\n  End\n" \
                                       b"  ProjectileObject = DgDozer\nEnd\n"
        manifest = json.loads(data["manifest.json"])
        files = {n: v for n, v in data.items() if n != "manifest.json"}
        package.write_package(edited, manifest, files)
        res = validatemod.validate(edited, [self.retail], mod_paths=[self.mod])
        self.assertTrue(any("DgDozer" in m for _r, m in res.errors), res.errors)

    def test_fatal_kinds_are_errors_even_when_the_mod_lacks_them(self):
        # Science is resolved while the INI files are read (INI::scanScience throws): never a warning
        self.assertIn("Science", validatemod.FATAL_KINDS)
        self.assertEqual(validatemod.FATAL_KINDS, frozenset(["Science", "CommandButton", "Locomotor"]))


# ---- 7/B/C. where the engine looks for files -------------------------------------------------------------------------
class SearchPaths(unittest.TestCase):
    def test_w3d_paths(self):
        self.assertEqual(search.w3d_paths("Foo", "german"), ["data/german/art/w3d/foo.w3d", "art/w3d/foo.w3d"])
        self.assertEqual(search.w3d_stem("Model", "FOO.BODY"), "foo")
        self.assertEqual(search.w3d_stem("Anim", "SKL.WALK"), "walk")
        self.assertIsNone(search.w3d_stem("Anim", "NODOT"))

    def test_texture_rules(self):
        self.assertEqual(search.texture_paths("X.tga", "")[:2], ["art/textures/x.dds", "art/textures/x.tga"])
        self.assertEqual(search.texture_paths("X.dds", ""), ["art/textures/x.dds"])      # no tga fallback
        self.assertEqual(search.texture_paths("F18d", ""), ["art/textures/f18d.dds", "art/textures/f18d.tga"])
        loc = search.texture_paths("x.tga", "chinese")
        self.assertEqual(loc[0], "data/chinese/art/textures/x.dds")

    def test_audio_paths(self):
        self.assertEqual(search.audio_paths("AudioFile", "Boom", "english"),
                         ["data/audio/sounds/english/boom.wav", "data/audio/sounds/boom.wav"])
        self.assertEqual(search.audio_paths("TrackFile", "m.mp3", ""), ["data/audio/tracks/m.mp3"])
        self.assertEqual(search.audio_paths("SpeechFile", "s.wav", "")[-1], "data/audio/speech/s.wav")

    def test_audio_settings_come_from_audiosettings_ini(self):
        v = Vfs()
        v.add_memory({"Data/INI/AudioSettings.ini": b"AudioSettings\n  AudioRoot = Data\\Sfx\n  SoundsFolder = Fx\n"
                                                    b"  SoundsExtension = ogg\nEnd\n"})
        s = search.audio_settings(v)
        self.assertEqual(search.audio_paths("AudioFile", "A", "", s), ["data/sfx/fx/a.ogg"])


class AssetLookup(Tmp):
    """Models, textures and sounds found the way the engine finds them."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.retail = cls.path_of(cls, "retail")
        fixtures.build_base(cls.retail)
        cls.mod = cls.path_of(cls, "mod")
        obj = """
Object LkHQ
  Side = LkSide
  DisplayName = OBJECT:LkHQ
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = lk_local
    End
    ConditionState = DAMAGED
      Model = lk_file.PART
    End
    ConditionState = REALLYDAMAGED
      Model = lk_gone
    End
  End
  KindOf = STRUCTURE SELECTABLE COMMANDCENTER
  Body = StructureBody ModuleTag_Body
    MaxHealth = 10.0
  End
  ShadowTexture = lk_icon_a
  ButtonImage = LK_IMG
  VoiceSelect = LkVoice
  UnitSpecificSounds
    VoiceFire = LkFire
  End
End
"""
        images = """
MappedImage LK_IMG
  Texture = lk_ui.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:8 Bottom:8
  Status = NONE
End
MappedImage LK_IMG2
  Texture = lk_ui_dds_only.dds
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:8 Bottom:8
  Status = NONE
End
"""
        sounds = """
AudioEvent LkVoice
  Sounds = lk_localized lk_mp3 lk_plain
End
AudioEvent LkFire
  Sounds = lk_localized
End
"""
        ptemplate = """
PlayerTemplate FactionLk
  Side = LkSide
  BaseSide = LkSide
  PlayableSide = Yes
  DisplayName = INI:LkSide
  StartingBuilding = LkHQ
End
"""
        files = {"Data\\INI\\Object\\Lk.ini": obj, "Data\\INI\\MappedImages\\HandCreated\\Lk.ini": images,
                 "Data\\INI\\SoundEffects\\Lk.ini": sounds, "Data\\INI\\PlayerTemplate\\Lk.ini": ptemplate,
                 "Data\\English\\generals.csf": stringsmod.build_csf({"INI:LkSide": "Lk", "OBJECT:LkHQ": "HQ"}),
                 # model only in the localized folder, one under Art/W3D with a dotted request
                 "Data\\English\\Art\\W3D\\LK_LOCAL.W3D": w3d_model("LK_LOCAL", "lk_main.tga"),
                 "Art\\W3D\\LK_FILE.W3D": w3d_model("LK_FILE", "lk_main.tga"),
                 "Art\\Textures\\lk_main.dds": b"dds main",
                 "Data\\English\\Art\\Textures\\lk_icon_a.tga": b"tga icon in the language folder",
                 "Art\\Textures\\lk_ui.dds": b"only a dds for a tga request",
                 "Art\\Textures\\lk_ui_dds_only.tga": b"a tga for a dds request",
                 "Data\\Audio\\Sounds\\English\\lk_localized.wav": b"RIFF localized",
                 "Data\\Audio\\Sounds\\lk_mp3.mp3": b"not a wav",
                 "Data\\Audio\\Sounds\\lk_plain.wav": b"RIFF plain"}
        write_big(os.path.join(cls.mod, "!Lk.big"), files)
        cls.out = cls.path_of(cls, "lk.zharmy")
        cls.c, cls.rep = convert([cls.mod], [cls.retail], Options(tag="LKX", faction="FactionLk"), cls.out)

    def path_of(self, *parts):
        return os.path.join(self.tmp, *parts)

    def names(self):
        return package.Package(self.out).names()

    def test_localized_model_is_found_and_copied(self):
        copied = {r["name"] for r in self.rep["assets"]["copied"] if r["kind"] == "Model"}
        self.assertIn("lk_local", copied)
        self.assertTrue(any(n.startswith("art/w3d/lkx") for n in self.names()))

    def test_dotted_model_request_loads_the_file_before_the_dot(self):
        copied = {r["name"] for r in self.rep["assets"]["copied"] if r["kind"] == "Model"}
        self.assertIn("lk_file", copied)
        ini_text = package.Package(self.out).read("army/ini/object.ini").decode()
        self.assertRegex(ini_text, r"Model = LKX[0-9A-Z]+\.PART")

    def test_missing_model_is_dangling_not_an_error(self):
        miss = {d["name"] for d in self.rep["danglingReferences"] if d["what"] == "model"}
        self.assertEqual(miss, {"lk_gone"})

    def test_tga_request_finds_the_dds_but_a_dds_request_does_not_find_a_tga(self):
        copied = {r["name"] for r in self.rep["assets"]["copied"] if r["kind"] == "Texture"}
        self.assertIn("lk_ui", copied)                                   # asked as .tga, the file is a .dds
        self.assertTrue(any(n.startswith("art/textures/") and n.endswith(".dds") for n in self.names()))
        missing = {r["name"]: r for r in self.rep["assets"]["missing"] if r["kind"] == "Texture"}
        self.assertNotIn("lk_ui_dds_only", missing)                       # MappedImage LK_IMG2 is not reachable
        # the unit test of the rule itself
        v = Vfs()
        v.add_memory({"art/textures/a.tga": b"1"})
        self.assertIsNone(search.first_existing(v, search.texture_paths("a.dds", "")))
        self.assertEqual(search.first_existing(v, search.texture_paths("a.tga", "")), "art/textures/a.tga")

    def test_texture_in_the_language_folder(self):
        copied = {r["name"] for r in self.rep["assets"]["copied"] if r["kind"] == "Texture"}
        self.assertIn("lk_icon_a", copied)

    def test_sounds_localized_first_wav_only(self):
        copied = {r["name"] for r in self.rep["assets"]["copied"] if r["kind"] == "AudioFile"}
        self.assertEqual(copied, {"lk_localized", "lk_plain"})
        missing = {r["name"] for r in self.rep["assets"]["missing"] if r["kind"] == "AudioFile"}
        self.assertEqual(missing, {"lk_mp3"})                             # the engine appends .wav
        self.assertTrue(any(n.startswith("data/audio/sounds/lkx_lk_localized") for n in self.names()))

    def test_package_validates_and_localized_ruleset_files_are_accepted(self):
        res = validatemod.validate(self.out, [self.retail], mod_paths=[self.mod])
        self.assertTrue(res.ok, res.errors)
        # a ruleset sound that exists only in a language folder satisfies a reference from a package
        game = self.path_of("localgame")
        write_big(os.path.join(game, "Audio.big"), {"Data\\Audio\\Sounds\\German\\ruleset_snd.wav": b"RIFF"})
        v = Vfs()
        v.add_tree(game, overwrite=False)
        tails = search.localized_tails(v)
        self.assertTrue(search.exists_any(v, ["data/audio/sounds/ruleset_snd.wav"], tails))
        self.assertFalse(search.exists_any(v, ["data/audio/sounds/ruleset_snd.wav"]))

    def test_missing_texture_hint_names_the_other_extension(self):
        from zharmy import assets
        v = Vfs()
        v.add_memory({"art/textures/p.tga": b"1", "data/german/art/w3d/m.w3d": b"x"})
        ap = assets.AssetPlanner(v, None, "ZZ", False, "english")
        self.assertIn("p.tga exists", ap._hint("Texture", "p.dds"))
        self.assertIn("data/german/art/w3d/m.w3d", ap._hint("Model", "m"))


# ---- D. W3D names defined in several files ---------------------------------------------------------------------------
class W3dNameCollision(Tmp):
    def test_two_files_defining_the_same_name_get_distinct_names(self):
        retail = self.path("retail")
        fixtures.build_base(retail)
        mod = self.path("mod")
        ini = """
Object CoHQ
  Side = CoSide
  DisplayName = OBJECT:CoHQ
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = co_a
    End
  End
  Draw = W3DModelDraw ModuleTag_Draw2
    DefaultConditionState
      Model = co_b
    End
  End
  KindOf = STRUCTURE SELECTABLE COMMANDCENTER
  Body = StructureBody ModuleTag_Body
    MaxHealth = 10.0
  End
End
PlayerTemplate FactionCo
  Side = CoSide
  BaseSide = CoSide
  PlayableSide = Yes
  DisplayName = INI:CoSide
  StartingBuilding = CoHQ
End
"""
        # co_a.w3d defines the render object "co_a" and also a hierarchy named "co_b" (the other file's stem);
        # co_b.w3d defines the object "co_b" with its own hierarchy "co_b"
        from .fixtures import w3d_hierarchy, w3d_hlod, w3d_mesh
        co_a = w3d_mesh("co_a", "BODY", "") + w3d_hierarchy("co_b") + w3d_hlod("co_a", "co_b", [(0, "co_a.BODY")])
        co_b = w3d_model("co_b", "")
        obj_ini, pt_ini = ini.split("PlayerTemplate FactionCo")
        write_big(os.path.join(mod, "!Co.big"), {
            "Data\\INI\\Object\\Co.ini": obj_ini, "Data\\INI\\PlayerTemplate\\Co.ini": "PlayerTemplate FactionCo" + pt_ini,
            "Art\\W3D\\CO_A.W3D": co_a, "Art\\W3D\\CO_B.W3D": co_b,
            "Data\\English\\generals.csf": stringsmod.build_csf({"INI:CoSide": "Co", "OBJECT:CoHQ": "HQ"})})
        out = self.path("co.zharmy")
        c, rep = convert([mod], [retail], Options(tag="COL", faction="FactionCo"), out)
        res = validatemod.validate(out, [retail], mod_paths=[mod])
        self.assertTrue(res.ok, res.errors)
        self.assertFalse(any("defined by both" in m for _r, m in res.errors))
        self.assertTrue(any("is defined by 2 files" in w for w in rep["warnings"]))
        # the file named like the name keeps it
        pkg = package.Package(out)
        from zharmy import w3d
        defined = {}
        for n in pkg.names():
            if n.startswith("art/w3d/"):
                for d in w3d.scan(w3d.parse(pkg.read(n))).defined:
                    defined.setdefault(d, []).append(n)
        self.assertTrue(all(len(v) == 1 for v in defined.values()), defined)
        # every file still refers to a hierarchy it defines itself or one that exists
        allnames = set(defined)
        for n in pkg.names():
            if n.startswith("art/w3d/"):
                sc = w3d.scan(w3d.parse(pkg.read(n)))
                self.assertTrue(sc.external() <= allnames or not sc.external(), (n, sc.external()))


# ---- A. INI parser ----------------------------------------------------------------------------------------------------
class IniParsing(Tmp):
    def test_eva_side_sounds_block_is_parsed(self):
        text = b"""EvaEvent LowPower
  Priority = 5
  SideSounds
    Side = America
    Sounds = EvaA1 EvaA2
  End
  SideSounds
    Side = China
    Sounds = EvaC
  End
End
Weapon After
End
"""
        r = inimod.Parser().parse(text, "eva.ini")
        self.assertEqual(r.errors, [])
        self.assertEqual([b.name for b in r.blocks], ["EvaEvent", "Weapon"])
        self.assertEqual(len(r.blocks[0].all("SideSounds")), 2)

    def test_expert_skill_block_in_aidata(self):
        r = inimod.Parser().parse(b"AIData\n  ExpertSkill\n    Kiting = Yes\n  End\n  SideInfo A\n  End\nEnd\n")
        self.assertEqual(r.errors, [])

    def test_control_characters_are_blanks_like_in_the_engine(self):
        data = b"Weapon\x0b\x0cA\n  PrimaryDamage\x01=\x011.0 ; c\r\nEnd\r\n\x1a\r\nWeapon B\nEnd\n\x1a"
        r = inimod.Parser().parse(data, "w.ini")
        self.assertEqual(r.errors, [])
        self.assertEqual([b.args for b in r.blocks], ["A", "B"])
        self.assertEqual(r.blocks[0].first("PrimaryDamage"), "1.0")
        r = inimod.Parser().parse(b"Weapon C\nEnd\nGarbage\0 here\nWeapon D\nEnd\n")      # NUL ends the line
        self.assertEqual([b.args for b in r.blocks], ["C", "D"])

    def test_unknown_lines_are_explained_and_summarised(self):
        data = b"Object A\n  Foo\n  End\n  Bar = 1\nEnd\nObject B\nEnd\n"
        r = inimod.Parser().parse(data, "o.ini")
        self.assertTrue(r.errors)
        self.assertIn("probably ended early", r.errors[0][1])
        errors = [("data/ini/object.ini", l, m) for l, m in r.errors] * 5
        text = format_ini_problems(errors, examples=2)
        self.assertIn("INI problems", text)
        self.assertIn("data/ini/object.ini:", text)
        self.assertIn("and ", text)
        full = format_ini_problems(errors, full=True)
        self.assertGreater(len(full.splitlines()), len(text.splitlines()))
        self.assertEqual(format_ini_problems([]).strip(), "No INI problems.")

    def test_inspect_lists_ini_problems(self):
        root = self.path("badini")
        write_big(os.path.join(root, "INIZH.big"), {
            "Data\\INI\\Object.ini": "Object A\n  Side = X\nEnd\nStray = 1\nObject B\nEnd\n",
            "Data\\INI\\PlayerTemplate.ini": "PlayerTemplate FactionX\n  Side = X\n  PlayableSide = Yes\nEnd\n"})
        res = inspect_factions([root])
        self.assertEqual(res["iniErrors"], 1)
        text = format_factions(res)
        self.assertIn("1 INI problem", text)
        self.assertIn("--ini-problems", text)
        code, out, err = run_cli("inspect", root, "--ini-problems", "-q")
        self.assertEqual(code, 0)
        self.assertIn("data/ini/object.ini:4", out)
        self.assertNotIn("--ini-problems to list", out)

    def test_every_nested_block_opener_of_the_engine_is_known_to_the_parser(self):
        known = set()
        for group in (inimod.OBJECT_BLOCKS, inimod.MODULE_HEADERS, inimod.WRAPPERS, inimod.DRAW_BLOCKS,
                      inimod.BEHAVIOR_BLOCKS, inimod.DECAL_BLOCKS, inimod.FX_NUGGETS, inimod.OCL_NUGGETS,
                      inimod.SKILLSETS, inimod.AIDATA_BLOCKS):
            known |= set(group)
        known |= {"SideInfo", "SkirmishBuildList", "Structure", "SideSounds"}
        # other file types, opaque to the converter (campaigns, UI schemes, challenge generals, transitions)
        other = {"AnimatingPart", "ImagePart", "LinePart", "Mission", "Window", "AliasConditionState"}
        unknown = [f for f in schema_data.NESTED_FIELDS if f not in known and f not in other
                   and not f.startswith("GeneralPersona")]
        self.assertEqual(unknown, [], "the engine reads these as blocks with their own End; teach ini.py")


# ---- 8. output volume -------------------------------------------------------------------------------------------------
class OutputVolume(unittest.TestCase):
    def test_warnings_are_summarised_on_the_console_and_complete_in_the_report(self):
        from zharmy.convert import Report, _warning_digest
        rep = Report()
        for i in range(250):
            rep.warn("string label OBJECT:X%d is in no string table" % i, "dangling string label")
        for i in range(5):
            rep.warn("model m%d is missing" % i, "dangling model")
        rep.warn("something else", "other")
        digest = _warning_digest(rep)
        self.assertLess(len(digest), 15)
        self.assertTrue(any("and 247 more" in d for d in digest))
        report = {
            "package": {"id": "a.b", "tag": "AB", "requires": ["zerohour"], "contentHash": "sha256:0", "entries": 1,
                        "faction": {"playerTemplate": "P", "side": "S", "displayName": "D", "ai": False}},
            "definitionsCopied": {}, "definitionsKeptAsReferences": {}, "modifiedStockDefinitions": [],
            "copiedBecauseTheyPointToChangedDefinitions": [], "assets": {"copied": [], "keptAsReferences": [],
                                                                         "missing": []},
            "strings": {"copied": [], "reference": [], "missing": []},
            "ai": {"skirmishBuildList": False, "sideInfo": False, "scripts": False},
            "notCarried": [], "unknownFields": [], "unresolvedReferences": [], "warnings": rep.warnings,
            "warningGroups": rep.kinds(), "notes": []}
        text = format_report(report)
        self.assertLess(len(text.splitlines()), 40)
        self.assertIn("and 247 more (see --report)", text)
        self.assertIn("string label: 250", text)
        self.assertEqual(len(report["warnings"]), 256)


# ---- 9. find ----------------------------------------------------------------------------------------------------------
class FindCommand(Tmp):
    def test_find_lists_providers_and_marks_the_winner(self):
        root = self.path("game")
        build_two_installs(root)
        os.makedirs(os.path.join(root, "zero hour", "Data"), exist_ok=True)
        fixtures.wr(os.path.join(root, "zero hour", "Data", "Marker.txt"), b"loose marker")
        code, out, err = run_cli("find", root, "*marker.txt")
        self.assertEqual(code, 0, err)
        self.assertIn("data/marker.txt", out)
        lines = out.splitlines()
        first = [l for l in lines if "->" in l][0]
        self.assertIn("loose file", first)                                 # a loose file beats every archive
        self.assertIn("INIZH.big", out)
        self.assertIn("INI.big", out)
        self.assertLess(out.index("INIZH.big"), out.index("generals"))
        code, out, err = run_cli("find", root, "*skirmishscripts*")
        self.assertIn("data/scripts/skirmishscripts.scb", out)
        self.assertIn("INI.big", out)
        code, out, err = run_cli("find", root, "*nothing-like-this*")
        self.assertEqual(code, 1)
        self.assertIn("no path matches", out)
        code, out, err = run_cli("find", root)
        self.assertEqual(code, 2)


# ---- E. names in a package --------------------------------------------------------------------------------------------
class DefinitionNames(unittest.TestCase):
    def test_name_rule(self):
        for ok in ("TK-XLocomotor", "AB_Name.1", "AB_x+y", "AB_x(1)"):
            self.assertTrue(validatemod.NAME_RE.match(ok), ok)
        for bad in ("AB_x,y", "AB_x;y", "AB_x=y", "AB x"):
            self.assertFalse(validatemod.NAME_RE.match(bad), bad)

    def test_validate_accepts_a_dash_and_rejects_a_comma(self):
        tmp = tempfile.mkdtemp(prefix="zharmy-names-")
        try:
            def pkg(name):
                files = {"army/ini/locomotor.ini": ("Locomotor AB_%s\n  Speed = 1.0\nEnd\n" % name).encode()}
                out = os.path.join(tmp, "p.zharmy")
                manifest = {"format": 1, "id": "ab.test", "tag": "AB", "name": "T", "version": "1.0.0",
                            "requires": [], "factions": [{"playerTemplate": "AB_F", "side": "AB_S",
                                                          "displayName": "F", "ai": False}], "authors": []}
                package.write_package(out, manifest, files)
                return validatemod.validate(out)
            self.assertFalse(any("bad Locomotor name" in m for _r, m in pkg("TK-XLocomotor").errors))
            self.assertTrue(any("bad Locomotor name" in m for _r, m in pkg("TK,X").errors))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
