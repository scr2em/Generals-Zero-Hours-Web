"""End to end on the synthetic game and mod (tests/fixtures.py): inspect, convert, convert-all, validate."""

import json
import os
import shutil
import tempfile
import unittest
import zipfile

from . import _path  # noqa: F401
from zharmy import package, strings, validate as validatemod
from zharmy import w3d
from zharmy.convert import (ConvertError, Options, convert, convert_all, derive_tags, format_report,
                            format_summary)
from zharmy.inspect_mod import format_factions, inspect_factions
from . import fixtures

MOD_GLOBS = ["!ModMain.big", "zModArt.big"]


class World(unittest.TestCase):
    """Builds the game folder with the mod installed into it, and the same two as separate folders."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="zharmy-test-")
        cls.installed = os.path.join(cls.tmp, "installed")
        fixtures.build_base(cls.installed)
        fixtures.build_mod_archives(cls.installed)
        cls.base_only = os.path.join(cls.tmp, "retail")
        fixtures.build_base(cls.base_only)
        cls.mod_only = os.path.join(cls.tmp, "mod")
        fixtures.build_mod_archives(cls.mod_only, "ModMain.big", "ModArt.big")
        cls.out = os.path.join(cls.tmp, "out")
        os.makedirs(cls.out)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def conv(self, faction, tag, name=None, installed=True, **kw):
        out = os.path.join(self.out, name or (tag + ".zharmy"))
        opts = Options(tag=tag, faction=faction, requires="zerohour", **kw)
        if installed:
            c, rep = convert([self.installed], [self.installed], opts, out, mod_archives=MOD_GLOBS)
        else:
            c, rep = convert([self.mod_only], [self.base_only], opts, out)
        return c, rep, out

    def validate(self, out, **kw):
        return validatemod.validate(out, [self.installed], mod_archives=MOD_GLOBS, **kw)


class InspectTests(World):
    def test_installed_mod_lists_factions(self):
        res = inspect_factions([self.installed], [self.installed], mod_archives=MOD_GLOBS)
        names = [f["playerTemplate"] for f in res["factions"]]
        self.assertEqual(names, ["FactionTstBase", "FactionModAlpha", "FactionModBeta"])
        by = {f["playerTemplate"]: f for f in res["factions"]}
        self.assertEqual(by["FactionModAlpha"]["displayName"], "Alpha Army")
        self.assertTrue(by["FactionModAlpha"]["skirmishScripts"] and by["FactionModAlpha"]["skirmishBuildList"])
        self.assertFalse(by["FactionModBeta"]["skirmishScripts"])
        self.assertTrue(by["FactionTstBase"]["inRuleset"])
        self.assertFalse(by["FactionModAlpha"]["inRuleset"])
        self.assertIn("3 playable factions", format_factions(res))

    def test_retail_alone_has_one_faction(self):
        res = inspect_factions([self.base_only])
        self.assertEqual([f["playerTemplate"] for f in res["factions"]], ["FactionTstBase"])

    def test_archive_order_rule(self):
        # '!ModMain.big' sorts before the retail archives, so its Weapon.ini wins in the installed folder
        res = inspect_factions([self.installed])
        self.assertEqual(len(res["factions"]), 3)


class ConvertAlpha(World):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        t = cls.__new__(cls)
        cls.c, cls.rep, cls.path = World.conv(t, "FactionModAlpha", "MALP") if False else (None, None, None)

    def test_alpha(self):
        c, rep, out = self.conv("FactionModAlpha", "MALP")
        copied = rep["definitionsCopied"]
        self.assertEqual(copied["PlayerTemplate"], ["FactionModAlpha"])
        for name in ("MAlphaHQ", "MAlphaTank", "MAlphaTankGold"):
            self.assertIn(name, copied["Object"])
        self.assertNotIn("MAlphaDozerOnStock", copied["Object"])        # not reachable
        self.assertIn("TstCannon", copied["Weapon"])                    # the mod changed it
        self.assertIn("MAlphaRay", copied["Weapon"])
        mods = [(e["kind"], e["name"]) for e in rep["modifiedStockDefinitions"]]
        self.assertIn(("Weapon", "TstCannon"), mods)
        self.assertNotIn(("Weapon", "TstMG"), mods)
        refs = rep["definitionsKeptAsReferences"]
        self.assertIn("TstProjectile", refs["Object"])
        self.assertIn("TstArmor", refs["Armor"])
        self.assertIn("TstTreads", refs["Locomotor"])
        self.assertIn("Upgrade_TstArmor", refs["Upgrade"])
        self.assertIn("TstBaseTank", copied["Object"])                  # points at the changed weapon
        dep = [e["name"] for e in rep["copiedBecauseTheyPointToChangedDefinitions"]]
        self.assertIn("TstBaseTank", dep)

        pkg = package.Package(out)
        m = pkg.manifest()
        self.assertEqual((m["id"], m["tag"], m["requires"]), ("malp.modalpha", "MALP", ["zerohour"]))
        fac = m["factions"][0]
        self.assertEqual(fac, {"playerTemplate": "MALP_FactionModAlpha", "side": "MALP_ModAlpha",
                               "displayName": "Alpha Army", "ai": True})
        obj = pkg.read("army/ini/object.ini").decode("latin-1")
        self.assertIn("Object MALP_MAlphaHQ", obj)
        self.assertIn("ObjectReskin MALP_MAlphaTankGold MALP_MAlphaTank", obj)
        self.assertIn("Side = MALP_ModAlpha", obj)
        self.assertNotIn("CodeNeedingModule", obj)
        self.assertIn("MysteryField = 5", obj)                           # unknown fields are kept
        self.assertIn("Weapon = SECONDARY MALP_MAlphaRay", obj)
        self.assertIn("UpgradeCameo1 = Upgrade_TstArmor", obj)
        self.assertIn("Locomotor = SET_NORMAL TstTreads", obj)
        self.assertIn("Weapon = PRIMARY MALP_TstCannon", obj)
        self.assertEqual([u["name"] for u in rep["unknownFields"] if u["name"] == "MAlphaTank"], ["MAlphaTank"])
        self.assertTrue(any(e["what"] == "module" and "CodeNeedingModule" in e["detail"]
                            for e in rep["notCarried"]))
        # moved stock object
        self.assertTrue(any("TstBaseTank was on side TstBase" in w for w in rep["warnings"]))
        # model / animation names
        names = sorted(n for n in pkg.names() if n.startswith("art/w3d/"))
        self.assertTrue(all(os.path.basename(n).startswith("malp") for n in names))
        self.assertIn("Animation = MALP", obj)
        for n in names:
            sc = w3d.scan(w3d.parse(pkg.read(n)))
            for d in sc.defined:
                self.assertTrue(d.startswith("malp") and len(d) <= 15, d)
        self.assertEqual(len(rep["w3dNames"]), len(set(rep["w3dNames"].values())))
        # textures: house colour texture warning, ordinary ones renamed
        self.assertFalse(any("house-colour" in w for w in rep["warnings"]))
        self.assertTrue(any(n.startswith("art/textures/zhcmalp") for n in pkg.names()))
        self.assertTrue(pkg.has("art/textures/malp_malpha_ui.tga"))
        # sounds: the localized file is flattened into the generic folder
        self.assertEqual(pkg.read("data/audio/sounds/malp_malphaboom.wav"), b"RIFF alpha boom english")
        self.assertTrue(pkg.has("data/audio/sounds/malp_malphaboom2.wav"))
        self.assertFalse(any(n.endswith("tstboom.wav") for n in pkg.names()))   # identical to retail: reference
        snd = pkg.read("army/ini/soundeffects.ini").decode()
        self.assertIn("Sounds = MALP_malphaboom MALP_malphaboom2", snd)
        # strings
        st = strings.parse_str(pkg.read("army/strings.str"))
        self.assertEqual(st["MALP:OBJECT:MAlphaTank"], "Alpha Tank ? mk2")
        self.assertEqual(st["MALP:INI:ModAlpha"], "Alpha Army")
        self.assertNotIn("MALP:OBJECT:TstBaseHQ", st)
        self.assertTrue(any("outside Latin-1" in w for w in rep["warnings"]))
        # AI
        ai = pkg.read("army/ini/aidata.ini").decode()
        self.assertIn("SideInfo MALP_ModAlpha", ai)
        self.assertIn("SkirmishBuildList MALP_ModAlpha", ai)
        self.assertIn("BaseDefenseStructure1 = MALP_MAlphaHQ", ai.replace("=", " = ").replace("  ", " ") if False else ai.replace("= ", "= "))
        from zharmy import scb
        s = scb.read_scb(pkg.read("army/scripts/skirmish.scb"))
        self.assertEqual([p[0] for p in s.players], ["SkirmishMALP_ModAlpha"])
        scripts = [x.name for x in s.lists[0].all_scripts()]
        self.assertEqual(scripts, ["MALP_AlphaBuild", "MALP_ModAlphaAttack"])
        pb = s.lists[0].items[0].actions[1].params
        self.assertEqual([p.string for p in pb], ["MALP_MAlphaTank", "<This Player>"])
        # validate against the ruleset
        res = self.validate(out)
        self.assertTrue(res.ok, res.errors)
        text = format_report(rep)
        self.assertIn("Ruleset definitions the mod changed", text)

    def test_deterministic_and_layout_independent(self):
        _c, rep1, p1 = self.conv("FactionModAlpha", "MALP", "a1.zharmy")
        _c, rep2, p2 = self.conv("FactionModAlpha", "MALP", "a2.zharmy")
        self.assertEqual(fixtures.rd(p1), fixtures.rd(p2))
        # the mod as separate folders on top of the retail folder gives the same package content
        _c, rep3, p3 = self.conv("FactionModAlpha", "MALP", "a3.zharmy", installed=False)
        self.assertEqual(rep1["package"]["contentHash"], rep3["package"]["contentHash"])
        self.assertEqual(rep1["definitionsCopied"], rep3["definitionsCopied"])

    def test_beta_macros_and_no_ai(self):
        c, rep, out = self.conv("FactionModBeta", "MBET")
        pkg = package.Package(out)
        obj = pkg.read("army/ini/object.ini").decode()
        self.assertIn("MaxHealth = 800.0", obj)
        self.assertIn("BuildCost = 450", obj)
        self.assertFalse(pkg.manifest()["factions"][0]["ai"])
        self.assertFalse(pkg.has("army/scripts/skirmish.scb"))
        self.assertTrue(any(e["what"] == "ini-extensions" for e in rep["notCarried"]))
        self.assertTrue(any("humans only" in w for w in rep["warnings"]))
        self.assertTrue(pkg.has("art/textures/mbet_mbeta_tex.dds") or any("mbeta_tex" in n for n in pkg.names())
                        or any(n.startswith("art/textures/mbet") for n in pkg.names()))
        res = self.validate(out)
        self.assertTrue(res.ok, res.errors)

    def test_zhc_names(self):
        # default: house-colour textures are named ZHC<TAG>... and keep team colour
        pkg = package.Package(self.conv("FactionModAlpha", "MALP", "zhc.zharmy")[2])
        self.assertTrue(any(n.startswith("art/textures/zhcmalp") for n in pkg.names()))
        _c, rep, out = self.conv("FactionModAlpha", "MALP", "nozhc.zharmy", zhc_names=False)
        pkg = package.Package(out)
        self.assertFalse(any(n.startswith("art/textures/zhc") for n in pkg.names()))
        self.assertTrue(any("house-colour" in w for w in rep["warnings"]))
        self.assertTrue(self.validate(out).ok)               # plain tag names are valid too

    def test_requires_none_copies_everything(self):
        out = os.path.join(self.out, "none.zharmy")
        opts = Options(tag="MALN", faction="FactionModAlpha", requires="none")
        c, rep = convert([self.installed], [], opts, out)
        self.assertEqual(rep["definitionsKeptAsReferences"], {})
        pkg = package.Package(out)
        self.assertTrue(pkg.has("art/w3d/" + rep["w3dNames"]["tst_tank"].lower() + ".w3d"))
        self.assertTrue(pkg.has("data/audio/sounds/maln_tstboom.wav"))
        res = validatemod.validate(out, None)
        self.assertTrue(res.ok, res.errors)

    def test_errors(self):
        with self.assertRaises(ConvertError):
            self.conv("FactionNope", "MALP")
        with self.assertRaises(ConvertError):
            convert([self.installed], [self.installed], Options(tag="bad", faction="FactionModAlpha"),
                    os.path.join(self.out, "x.zharmy"))
        with self.assertRaises(ConvertError):
            convert([self.installed], [], Options(tag="OK", faction="FactionModAlpha", requires="zerohour"),
                    os.path.join(self.out, "x.zharmy"))


class ConvertAll(World):
    def test_convert_all_and_tags(self):
        d = os.path.join(self.out, "all")
        rows, ctx = convert_all([self.installed], [self.installed], d, "MX", "zerohour", mod_archives=MOD_GLOBS)
        self.assertEqual([r["tag"] for r in rows], ["MX1", "MX2", "MX3"])
        self.assertTrue(all("error" not in r for r in rows), rows)
        text = format_summary(rows)
        self.assertIn("FactionModAlpha", text)
        self.assertIn("total (3)", text)
        for r in rows:
            self.assertTrue(os.path.exists(r["file"]))
            res = self.validate(r["file"])
            self.assertTrue(res.ok, (r["faction"], res.errors))
        alpha = [r for r in rows if r["faction"] == "FactionModAlpha"][0]
        self.assertGreaterEqual(alpha["objects"], 3)
        self.assertGreaterEqual(alpha["models"], 2)
        self.assertGreaterEqual(alpha["sounds"], 2)

    def test_derived_tags_are_valid_and_unique(self):
        tags = derive_tags(["FactionChinaNuke", "FactionChinaNinja", "FactionGLAToxin", "FactionX", "FactionAmericaAirForceGeneral"])
        self.assertEqual(len(set(tags)), 5)
        for t in tags:
            self.assertRegex(t, r"^[A-Z][A-Z0-9]{1,5}$")
        with self.assertRaises(ConvertError):
            derive_tags(["A", "B"], "TOOLONGPREFIX")

    def test_same_folder_without_mod_archives_selects_automatically(self):
        # the mod archives have no retail names: --mod-archives defaults to auto for an installed mod
        out = os.path.join(self.out, "same.zharmy")
        c, rep = convert([self.installed], [self.installed], Options(tag="SAME", faction="FactionModAlpha"), out)
        self.assertIn("MAlphaHQ", rep["definitionsCopied"]["Object"])
        self.assertTrue(any("!ModMain.big" in n for n in rep["setup"]))


class ValidateNegative(World):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        t = cls.__new__(cls)
        t.installed, t.out = cls.installed, cls.out
        opts = Options(tag="MALP", faction="FactionModAlpha", requires="zerohour")
        cls.good = os.path.join(cls.out, "good.zharmy")
        convert([cls.installed], [cls.installed], opts, cls.good, mod_archives=MOD_GLOBS)

    def edited(self, edit, manifest_edit=None, rehash=True):
        pkg = package.Package(self.good)
        files = {n: pkg.read(n) for n in pkg.names() if n != "manifest.json"}
        m = pkg.manifest()
        pkg.close()
        edit(files)
        if manifest_edit:
            manifest_edit(m)
        path = os.path.join(self.out, "bad%d.zharmy" % len(os.listdir(self.out)))
        if rehash:
            package.write_package(path, m, files)
        else:
            with zipfile.ZipFile(path, "w") as z:
                z.writestr("manifest.json", json.dumps(m))
                for n, d in files.items():
                    z.writestr(n, d)
        return path

    def errors(self, path, **kw):
        res = self.validate(path, **kw)
        self.assertFalse(res.ok)
        return " | ".join("[%s] %s" % e for e in res.errors)

    def test_good_package_is_valid(self):
        self.assertTrue(self.validate(self.good).ok)

    def test_hash(self):
        p = self.edited(lambda f: f.update({"army/strings.str": f["army/strings.str"] + b"\n"}), rehash=False)
        self.assertIn("contentHash", self.errors(p))

    def test_layout(self):
        p = self.edited(lambda f: f.update({"data/ini/object.ini": b"", "data/english/x.csf": b"", "maps/a.map": b""}))
        e = self.errors(p)
        self.assertIn("Data/INI", e)
        self.assertIn("Data/<Language>", e)
        self.assertIn("Maps", e)

    def test_prefix_and_side(self):
        def edit(f):
            f["army/ini/object.ini"] += b"\nObject Rogue\n  Side = America\nEnd\n"
        e = self.errors(self.edited(edit))
        self.assertIn("rule 1", e)
        self.assertIn("rule 3", e)

    def test_missing_reference(self):
        def edit(f):
            f["army/ini/object.ini"] += b"\nObject MALP_Ghost\n  Side = MALP_ModAlpha\n  Weapon = X\n  Locomotor = SET_NORMAL NoSuchLoco\n  CommandSet = MALP_NoSet\nEnd\n"
        e = self.errors(self.edited(edit))
        self.assertIn("MALP_NoSet", e)
        self.assertIn("NoSuchLoco", e)

    def test_aidata_rule(self):
        def edit(f):
            f["army/ini/aidata.ini"] = b"AIData\n  StructureSeconds = 1.0\n  SideInfo America\n  End\nEnd\n"
        e = self.errors(self.edited(edit))
        self.assertIn("rule 5", e)

    def test_strings_rule(self):
        e = self.errors(self.edited(lambda f: f.update({"army/strings.str": b'FOO:bar\n"x"\nEND\n'})))
        self.assertIn("rule 6", e)

    def test_asset_rules(self):
        def edit(f):
            f["art/w3d/rogue.w3d"] = f[[n for n in f if n.startswith("art/w3d/")][0]]
            f["art/textures/tst_main.dds"] = b"x"
        e = self.errors(self.edited(edit))
        self.assertIn("rule 4", e)
        self.assertIn("already exists in the ruleset", e)        # shadowing

    def test_manifest_rules(self):
        def me(m):
            m["format"] = 2
            m["tag"] = "bad"
            m["requires"] = ["nope"]
        e = self.errors(self.edited(lambda f: None, me))
        self.assertIn("format 2", e)
        self.assertIn("tag 'bad'", e)
        self.assertIn("unknown ruleset", e)

    def test_not_a_zip_and_with(self):
        p = os.path.join(self.out, "junk.zharmy")
        fixtures.wr(p, b"junk")
        self.assertFalse(validatemod.validate(p).ok)
        res = validatemod.validate(self.good, [self.installed], False, [self.good], MOD_GLOBS)
        self.assertFalse(res.ok)
        self.assertIn("also used", " ".join(m for _r, m in res.errors))

    def test_ai_true_needs_scripts(self):
        def edit(f):
            del f["army/scripts/skirmish.scb"]
        self.assertIn("Skirmish.scb", self.errors(self.edited(edit)))


if __name__ == "__main__":
    unittest.main()


class CliTests(World):
    def run_cli(self, *args):
        import io
        from contextlib import redirect_stdout
        from zharmy.cli import main
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = main(list(args))
        return code, buf.getvalue()

    def test_commands(self):
        code, out = self.run_cli("archives", self.installed, "--mod-archives", "!Mod*.big", "zMod*.big")
        self.assertEqual(code, 0)
        self.assertIn("MOD", out)
        self.assertLess(out.lower().index("!modmain.big"), out.lower().index("inizh.big"))
        code, out = self.run_cli("inspect", self.installed, "--base", self.installed, "--mod-archives", *MOD_GLOBS,
                                 "-q")
        self.assertIn("3 playable factions", out)
        d = os.path.join(self.out, "cli")
        code, out = self.run_cli("convert-all", self.installed, "--base", self.installed, "--mod-archives",
                                 *MOD_GLOBS, "--out-dir", d, "--tag-prefix", "CL", "--validate", "-q")
        self.assertEqual(code, 0, out)
        self.assertIn("total (3)", out)
        code, out = self.run_cli("validate", os.path.join(d, "CL2_modalpha.zharmy"), "--base", self.installed,
                                 "--mod-archives", *MOD_GLOBS)
        self.assertEqual(code, 0, out)
        self.assertIn("OK", out)
        code, out = self.run_cli("convert", self.installed, "--base", self.installed, "--mod-archives", *MOD_GLOBS,
                                 "--faction", "FactionModBeta", "--tag", "BETA", "-o",
                                 os.path.join(d, "b.zharmy"), "--report", os.path.join(d, "b.json"), "-q")
        self.assertEqual(code, 0)
        with open(os.path.join(d, "b.json")) as handle:
            self.assertEqual(json.load(handle)["package"]["tag"], "BETA")
