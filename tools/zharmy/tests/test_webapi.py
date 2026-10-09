"""The in-browser importer's entry points (webapi.py), on the synthetic game and mod, without a browser."""

import json
import os
import shutil
import tempfile
import unittest

from . import _path  # noqa: F401
from zharmy import validate as validatemod, webapi
from zharmy.convert import Options, convert
from zharmy.inspect_mod import inspect_factions
from . import fixtures

MOD = ["!ModMain.big", "zModArt.big"]


class WebApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="zharmy-web-")
        cls.game = os.path.join(cls.tmp, "game")
        fixtures.build_base(cls.game)
        fixtures.build_mod_archives(cls.game)
        cls.mod_only = os.path.join(cls.tmp, "mod")
        fixtures.build_mod_archives(cls.mod_only)
        cls.retail = os.path.join(cls.tmp, "retail")
        fixtures.build_base(cls.retail)

    @classmethod
    def tearDownClass(cls):
        webapi.close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def open(self, mod, base, requires="zerohour", archives=None, name="Testmod"):
        params = {"modPaths": mod, "basePaths": base, "requires": requires, "modArchives": archives, "modName": name}
        lines = []
        answer = json.loads(webapi.open_json(json.dumps(params), lines.append))
        return answer, lines

    def test_installed_mod_lists_three_armies(self):
        answer, lines = self.open([self.game], [self.game], archives=MOD)
        self.assertTrue(answer["ok"], answer)
        names = [f["playerTemplate"] for f in answer["factions"]]
        self.assertEqual(names, ["FactionTstBase", "FactionModAlpha", "FactionModBeta"])
        by = {f["playerTemplate"]: f for f in answer["factions"]}
        self.assertTrue(by["FactionModAlpha"]["skirmishBuildList"] and by["FactionModAlpha"]["skirmishScripts"])
        self.assertFalse(by["FactionModBeta"]["skirmishScripts"])
        self.assertTrue(by["FactionTstBase"]["inRuleset"])
        self.assertFalse(by["FactionModAlpha"]["inRuleset"])
        self.assertEqual(by["FactionModAlpha"]["objects"], 3)
        self.assertEqual([f["suggestedTag"] for f in answer["factions"]], ["TB", "MA", "MB"])
        self.assertTrue(any("loading" in line for line in lines))

    def test_the_command_line_inspect_agrees(self):
        cli = inspect_factions([self.game], [self.game], mod_archives=MOD)
        answer, _ = self.open([self.game], [self.game], archives=MOD)
        keys = ("playerTemplate", "side", "displayName", "skirmishBuildList", "skirmishScripts", "inRuleset")
        self.assertEqual([{k: f[k] for k in keys} for f in cli["factions"]], [{k: f[k] for k in keys} for f in answer["factions"]])

    def test_convert_matches_the_command_line_byte_for_byte(self):
        answer, _ = self.open([self.game], [self.game], archives=MOD)
        out = os.path.join(self.tmp, "out-web")
        row = json.loads(webapi.convert_json(json.dumps({"playerTemplate": "FactionModAlpha", "tag": "MA", "id": "testmod.modalpha",
                                                          "name": "Alpha Army", "modName": "Testmod"}), out))
        self.assertTrue(row["ok"], row)
        self.assertTrue(row["valid"])
        self.assertEqual((row["objects"], row["models"], row["textures"], row["sounds"]), (4, 4, 4, 2))
        self.assertTrue(row["ai"])
        self.assertIn("testmod.modalpha.zharmy: OK", row["validation"])
        cli_out = os.path.join(self.tmp, "cli.zharmy")
        opts = Options(tag="MA", faction="FactionModAlpha", id="testmod.modalpha", name="Alpha Army", source_mod="Testmod")
        convert([self.game], [self.game], opts, cli_out, mod_archives=MOD)
        with open(row["file"], "rb") as a, open(cli_out, "rb") as b:
            self.assertEqual(a.read(), b.read())

    def test_a_new_name_is_the_name_in_the_game(self):
        import zipfile
        answer, _ = self.open([self.game], [self.game], archives=MOD)
        row = json.loads(webapi.convert_json(json.dumps({"playerTemplate": "FactionModAlpha", "tag": "MA", "id": "testmod.alpha2",
                                                          "name": "Alpha Prime"}), os.path.join(self.tmp, "out-rename")))
        self.assertTrue(row["ok"] and row["valid"], row)
        z = zipfile.ZipFile(row["file"])
        manifest = json.loads(z.read("manifest.json"))
        self.assertEqual((manifest["name"], manifest["factions"][0]["displayName"]), ("Alpha Prime", "Alpha Prime"))
        self.assertIn(b'"Alpha Prime"', z.read("army/strings.str"))
        self.assertNotIn(b"Alpha Army", z.read("army/strings.str"))

    def test_a_faction_that_cannot_be_converted_is_a_row_with_the_converters_message(self):
        answer, _ = self.open([self.game], [self.game], archives=MOD)
        row = json.loads(webapi.convert_json(json.dumps({"playerTemplate": "FactionNoSuch", "tag": "NS", "id": "testmod.nosuch"}),
                                             os.path.join(self.tmp, "out-err")))
        self.assertFalse(row["ok"])
        self.assertIn("FactionNoSuch", row["error"])
        row = json.loads(webapi.convert_json(json.dumps({"playerTemplate": "FactionModAlpha", "tag": "a b", "id": "testmod.alpha"}),
                                             os.path.join(self.tmp, "out-err")))
        self.assertFalse(row["ok"])
        self.assertIn("2 to 6 capital letters", row["error"])

    def test_separate_mod_folder_and_self_contained(self):
        answer, _ = self.open([self.mod_only], [self.retail])
        self.assertTrue(answer["ok"], answer)
        self.assertEqual(len(answer["factions"]), 3)
        answer, _ = self.open([self.game], [], requires="none")
        self.assertTrue(answer["ok"], answer)
        row = json.loads(webapi.convert_json(json.dumps({"playerTemplate": "FactionModAlpha", "tag": "MA", "id": "testmod.modalpha"}),
                                             os.path.join(self.tmp, "out-none")))
        self.assertTrue(row["ok"] and row["valid"], row)
        res = validatemod.validate(row["file"])
        self.assertTrue(res.ok)
        self.assertEqual(res.manifest["requires"], [])

    def test_no_ruleset_says_what_to_do_in_plain_words(self):
        answer, _ = self.open([self.game], [])
        self.assertFalse(answer["ok"])
        self.assertIn("Zero Hour", answer["error"])
        self.assertNotIn("--", answer["error"])

    def test_auto_is_the_default_selection_and_retail_names_come_from_the_converter(self):
        answer, _ = self.open([self.game], [self.game], archives="auto")
        self.assertTrue(answer["ok"], answer)
        self.assertEqual([f["playerTemplate"] for f in answer["factions"]], ["FactionTstBase", "FactionModAlpha", "FactionModBeta"])
        names = ["INIZH.big", "!ModMain.big", "AudioGermanZH.big", "sub/zModArt.big", "Music.big"]
        self.assertEqual(json.loads(webapi.retail_json(json.dumps(names))), [True, False, True, False, True])

    def test_archive_names_with_glob_characters_match_exactly(self):
        self.assertEqual(webapi._glob_exact(["a/[x]*.big", "b?.big"]), ["a/[[]x][*].big", "b[?].big"])

    def test_loaded_ruleset_gives_the_same_verdict_as_reading_it_again(self):
        answer, _ = self.open([self.game], [self.game], archives=MOD)
        row = json.loads(webapi.convert_json(json.dumps({"playerTemplate": "FactionModBeta", "tag": "MB", "id": "testmod.modbeta"}),
                                             os.path.join(self.tmp, "out-val")))
        again = validatemod.validate(row["file"], [self.game], True, (), MOD)
        self.assertEqual(again.ok, row["valid"])
        self.assertEqual(["[%s] %s" % e for e in again.errors], row["validationErrors"])


if __name__ == "__main__":
    unittest.main()
