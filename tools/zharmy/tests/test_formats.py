"""Readers and writers: BIG, VFS, INI, strings, W3D, SCB, ZIP package."""

import os
import struct
import tempfile
import unittest

from . import _path  # noqa: F401
from zharmy import ini, package, scb, strings, w3d
from zharmy.bigfile import BigArchive, BigError, BigWriter
from zharmy.vfs import Vfs, glob_matcher
from . import fixtures


class BigTests(unittest.TestCase):
    def test_roundtrip_and_header_variants(self):
        with tempfile.TemporaryDirectory() as d:
            w = BigWriter()
            w.add("Data\\INI\\Object.ini", b"hello")
            w.add("art/w3d/x.w3d", b"\0\1\2")
            p = os.path.join(d, "a.big")
            w.write(p)
            a = BigArchive(p)
            self.assertEqual(sorted(a.names()), ["Data/INI/Object.ini", "art/w3d/x.w3d"])
            self.assertEqual(a.read("Data/INI/Object.ini"), b"hello")
            raw = fixtures.rd(p)
            q = os.path.join(d, "b.big")
            fixtures.wr(q, b"BIG4" + raw[4:])
            self.assertEqual(BigArchive(q).read("art/w3d/x.w3d"), b"\0\1\2")
            bad = os.path.join(d, "c.big")
            fixtures.wr(bad, b"NOPE" + raw[4:])
            with self.assertRaises(BigError):
                BigArchive(bad)
            trunc = os.path.join(d, "d.big")
            fixtures.wr(trunc, raw[:30])
            with self.assertRaises(BigError):
                BigArchive(trunc)


class VfsTests(unittest.TestCase):
    def _big(self, d, name, files):
        w = BigWriter()
        for k, v in files.items():
            w.add(k, v)
        w.write(os.path.join(d, name))

    def test_engine_rules(self):
        with tempfile.TemporaryDirectory() as d:
            self._big(d, "a.big", {"Data\\x.txt": b"A", "Data\\only_a.txt": b"A"})
            self._big(d, "B.big", {"data\\X.TXT": b"B"})
            # game folder: the first archive (alphabetical) wins
            v = Vfs()
            v.add_tree(d, overwrite=False)
            self.assertEqual(v.read("DATA/x.txt"), b"A")
            # mod folder: the last archive wins
            v = Vfs()
            v.add_tree(d, overwrite=True)
            self.assertEqual(v.read("data/x.txt"), b"B")
            self.assertEqual(v.read("Data/Only_A.txt"), b"A")
            # loose files beat archives
            os.makedirs(os.path.join(d, "Data"))
            fixtures.wr(os.path.join(d, "Data", "x.txt"), b"LOOSE")
            v = Vfs()
            v.add_tree(d, overwrite=False)
            self.assertEqual(v.read("data/x.txt"), b"LOOSE")
            self.assertTrue(v.exists("DATA\\ONLY_A.TXT"))

    def test_unreadable_archive_is_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            self._big(d, "good.big", {"a.txt": b"1"})
            fixtures.wr(os.path.join(d, "bad.big"), b"not an archive at all, really not")
            os.makedirs(os.path.join(d, "Data", "INI"))
            self._big(os.path.join(d, "Data", "INI"), "INIZH.big", {"dup.txt": b"x"})
            v = Vfs()
            v.add_tree(d, overwrite=False)
            self.assertEqual(len(v.problems), 1)
            self.assertTrue(v.exists("a.txt"))
            self.assertFalse(v.exists("dup.txt"))          # the duplicate Data/INI/INIZH.big is skipped

    def test_layers_and_exclude(self):
        with tempfile.TemporaryDirectory() as d:
            self._big(d, "game.big", {"f.txt": b"game"})
            self._big(d, "!mod.big", {"f.txt": b"mod", "g.txt": b"g"})
            full = Vfs()
            full.add_tree(d, overwrite=False)
            self.assertEqual(full.read("f.txt"), b"mod")          # '!' sorts first and wins
            base = Vfs()
            base.add_tree(d, overwrite=False, exclude=glob_matcher(["!mod*.big"]))
            self.assertEqual(base.read("f.txt"), b"game")
            self.assertFalse(base.exists("g.txt"))
            self.assertFalse(full.same_file(base, "f.txt"))
            self.assertTrue(Vfs().add_memory({"a": b"1"}) is not None)


class IniTests(unittest.TestCase):
    SRC = """
; comment
Object Thing  ; trailing
  Side = Foo
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = abc
    End
    ConditionState = DAMAGED
      Model = abd
    End
    AliasConditionState = REALLYDAMAGED
  End
  Behavior = AIUpdateInterface Tag
    Turret
      TurretTurnRate = 5
    End
  End
  ArmorSet
    Armor = A
  End
  RemoveModule ModuleTag_X
  AddModule
    Behavior = Foo Tag2
    End
  End
END

FXList FX
  Sound
    Name = Snd
  end
  ViewShake
    Type = SUBTLE
  End
End

ObjectReskin Thing2 Thing
  Draw = W3DModelDraw M
  End
End
"""

    def test_structure(self):
        f = ini.Parser().parse(self.SRC)
        self.assertEqual(f.errors, [])
        self.assertEqual([b.name for b in f.blocks], ["Object", "FXList", "ObjectReskin"])
        obj = f.blocks[0]
        draw = obj.all("Draw")[0]
        self.assertEqual([c.name for c in draw.children], ["DefaultConditionState", "ConditionState",
                                                          "AliasConditionState"])
        self.assertTrue(draw.children[0].is_block and not draw.children[2].is_block)
        self.assertTrue(obj.all("Behavior")[0].children[0].is_block)       # Turret
        self.assertFalse(obj.all("RemoveModule")[0].is_block)
        self.assertTrue(obj.all("AddModule")[0].is_block)
        self.assertEqual(f.blocks[2].args, "Thing2 Thing")

    def test_emit_roundtrip(self):
        f = ini.Parser().parse(self.SRC)
        text = ini.emit_text(f.blocks)
        g = ini.Parser().parse(text)
        self.assertEqual(g.errors, [])
        self.assertEqual([ini.canonical(a) for a in f.blocks], [ini.canonical(b) for b in g.blocks])
        self.assertEqual(ini.emit_text(g.blocks), text)

    def test_missing_end_is_reported(self):
        f = ini.Parser().parse("Weapon X\n  A = 1\n")
        self.assertTrue(f.errors)

    def test_unknown_top_level_and_opaque(self):
        f = ini.Parser().parse("Bogus X\nEnd\nControlBarScheme S\n  Foo\n    Bar = 1\n  End\nEnd\nWeapon W\n A = 1\nEnd\n")
        self.assertEqual([b.name for b in f.blocks], ["ControlBarScheme", "Weapon"])
        self.assertTrue(f.errors)
        self.assertEqual(f.opaque, [("ControlBarScheme", "S")])

    def test_macros_includes_and_slash_comments(self):
        files = {"inc.ini": b"#define HP 77\n"}

        def resolver(target, including):
            return files.get(target)
        text = '#include "inc.ini"\n// note\nObject X\n  MaxHealth = HP\nEnd\n'
        f = ini.Parser(resolver).parse(text, "main.ini")
        self.assertEqual(f.blocks[0].first("MaxHealth"), "77")
        self.assertTrue(f.extensions)

    def test_tokens_ignore_equals_and_spacing(self):
        f = ini.Parser().parse("Weapon W\n  A   =   1   2 \n  B 3\nEnd\n")
        self.assertEqual(f.blocks[0].first("A"), "1   2")
        self.assertEqual(f.blocks[0].first("B"), "3")
        self.assertEqual(ini.canonical(f.blocks[0]), "weapon w\na 1 2\nb 3\nend")


class StringTests(unittest.TestCase):
    def test_csf_and_str(self):
        data = {"A:B": "Hello", "C": "Line1\nLine2 \"q\" \\ x"}
        self.assertEqual(strings.parse_csf(strings.build_csf(data)), data)
        text = strings.build_str(data)
        self.assertEqual(strings.parse_str(text), data)
        with self.assertRaises(strings.StringsError):
            strings.parse_csf(b"nope")

    def test_latin1_and_dups(self):
        t, bad = strings.to_latin1("a–bé")
        self.assertEqual((t, bad), ("a?bé", 1))
        dups = []
        strings.parse_str(b'L\n"a"\nEND\nL\n"b"\nEND\n', dups)
        self.assertEqual(dups, ["L"])

    def test_load_from_vfs(self):
        v = Vfs()
        v.add_memory({"Data/English/generals.csf": strings.build_csf({"X": "1"}),
                      "Data/German/generals.csf": strings.build_csf({"X": "2"})})
        self.assertEqual(strings.load_strings(v).get("X"), "1")
        self.assertEqual(strings.load_strings(v, "german").get("x"), "2")


class W3dTests(unittest.TestCase):
    def test_parse_serialize_scan_rename(self):
        data = fixtures.w3d_model("ABC_TANK", "tex_one.tga", hierarchy="ABC_SKL")
        chunks = w3d.parse(data)
        self.assertEqual(w3d.serialize(chunks), data)
        sc = w3d.scan(chunks)
        self.assertEqual(sc.defined, {"abc_tank"})
        self.assertEqual(sc.hierarchies, {"abc_skl"})
        self.assertEqual(sc.external(), {"abc_skl"})
        self.assertEqual(sc.textures, ["tex_one.tga"])
        w3d.rename(chunks, {"abc_tank": "XY001", "abc_skl": "XY002"}, {"tex_one.tga": "XY003.tga"})
        out = w3d.serialize(chunks)
        sc2 = w3d.scan(w3d.parse(out))
        self.assertEqual(sc2.defined, {"xy001"})
        self.assertEqual(sc2.hierarchies, {"xy002"})
        self.assertEqual(sc2.textures, ["XY003.tga"])
        self.assertIn(b"XY001.BODY", out)           # object part of full names is kept
        self.assertNotIn(b"ABC_TANK", out)

    def test_anim_and_errors(self):
        a = w3d.parse(fixtures.w3d_anim("WALK", "SKL"))
        sc = w3d.scan(a)
        self.assertEqual((sc.defined, sc.hierarchies), ({"walk"}, {"skl"}))
        w3d.rename(a, {"walk": "ZZ1", "skl": "ZZ2"}, {})
        self.assertIn(b"ZZ1", w3d.serialize(a))
        with self.assertRaises(w3d.W3dError):
            w3d.parse(b"\1\0\0\0\xff\0\0\0")
        with self.assertRaises(w3d.W3dError):
            w3d._put(b"\0" * 20, 0, 4, "toolong")


class ScbTests(unittest.TestCase):
    def test_roundtrip_and_rename(self):
        data = fixtures.make_scb({"SkirmishFoo": [("Build", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")]),
                                                              ("X", 61, [("OBJECT_TYPE", "Tank"),
                                                                         ("SCRIPT", "Build"), ("SIDE", "SkirmishFoo")])])],
                                  "Civilian": []})
        s = scb.read_scb(data)
        self.assertEqual(scb.write_scb(s), data)
        sl, teams, name = scb.extract_for_player(s, "skirmishfoo")
        self.assertEqual(name, "SkirmishFoo")
        r = scb.Renamer("T_", {"Object": {"tank": "T_Tank"}}, {"skirmishfoo": "SkirmishT_Foo"})
        r.script_list(sl)
        script = next(sl.all_scripts())
        self.assertEqual(script.name, "T_Build")
        ps = script.actions[1].params
        self.assertEqual([p.string for p in ps], ["T_Tank", "T_Build", "SkirmishT_Foo"])
        self.assertEqual(script.actions[0].params[0].string, "<This Team>")
        t = r.team(teams[1], "SkirmishFoo", "SkirmishT_Foo")
        self.assertEqual(t.get("teamOwner"), "SkirmishT_Foo")
        self.assertEqual(t.get("teamName"), "T_TeamSkirmishFooInfantry")
        self.assertEqual(r.team(teams[0], "SkirmishFoo", "SkirmishT_Foo").get("teamName"), "teamSkirmishT_Foo")
        out = scb.Scb()
        out.players = [("SkirmishT_Foo", scb.Dict())]
        out.lists = [sl]
        out.teams = [t]
        again = scb.read_scb(scb.write_scb(out))
        self.assertEqual(again.players[0][0], "SkirmishT_Foo")

    def test_bad_input(self):
        with self.assertRaises(scb.ScbError):
            scb.read_scb(b"XXXX")


class PackageTests(unittest.TestCase):
    def test_hash_determinism_and_reading(self):
        files = {"Army/INI/Object.ini": b"Object T_A\nEnd\n", "art/w3d/t001.w3d": lambda: b"\0" * 10}
        with tempfile.TemporaryDirectory() as d:
            m = {"format": 1, "id": "t.a", "tag": "T", "name": "n"}
            p1, p2 = os.path.join(d, "1.zharmy"), os.path.join(d, "2.zharmy")
            w1 = package.write_package(p1, m, files)
            package.write_package(p2, m, files)
            self.assertEqual(fixtures.rd(p1), fixtures.rd(p2))
            pkg = package.Package(p1)
            self.assertEqual(pkg.names(), ["army/ini/object.ini", "art/w3d/t001.w3d", "manifest.json"])
            self.assertEqual(pkg.manifest()["contentHash"], pkg.compute_hash())
            # the documented definition, computed independently
            import hashlib
            import zlib
            h = hashlib.sha256()
            for n, data in (("army/ini/object.ini", b"Object T_A\nEnd\n"), ("art/w3d/t001.w3d", b"\0" * 10)):
                h.update(n.encode() + b"\0" + struct.pack("<Q", len(data)) + struct.pack("<I", zlib.crc32(data)))
            self.assertEqual(w1["contentHash"], "sha256:" + h.hexdigest())


if __name__ == "__main__":
    unittest.main()
