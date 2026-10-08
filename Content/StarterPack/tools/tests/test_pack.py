"""The whole pack: the build is deterministic, the manifest matches the files, and every reference resolves.

Building takes about twenty seconds, so the pack is built once for all tests in this file; set
STARTERPACK_SKIP_SLOW=1 to skip them.
"""
import hashlib
import json
import os
import shutil
import tempfile
import unittest

import _path  # noqa: F401
import build_pack
import validate_pack
from spk.bigfile import read_big

SKIP = os.environ.get("STARTERPACK_SKIP_SLOW") == "1"


@unittest.skipIf(SKIP, "slow")
class PackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="starterpack-test-")
        cls.files = build_pack.collect()
        cls.out = os.path.join(cls.tmp, "out")
        build_pack.main([cls.out])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_second_build_is_byte_identical(self):
        again = build_pack.collect()
        self.assertEqual(sorted(again), sorted(self.files))
        for name in again:
            self.assertEqual(hashlib.sha256(again[name]).digest(), hashlib.sha256(self.files[name]).digest(), name)

    def test_manifest_lists_every_file_with_matching_hash(self):
        root = os.path.join(self.out, "StarterPack")
        manifest = json.load(open(os.path.join(root, "manifest.json")))
        listed = {e["path"]: e for e in manifest["files"]}
        self.assertEqual(set(listed), set(self.files))
        for path, entry in listed.items():
            data = open(os.path.join(root, *path.split("/")), "rb").read()
            self.assertEqual(entry["size"], len(data), path)
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest(), path)

    def test_all_paths_are_lower_case_and_relative(self):
        for path in self.files:
            self.assertEqual(path, path.lower())
            self.assertFalse(path.startswith("/") or ".." in path.split("/"), path)

    def test_required_files_exist(self):
        for path in ("generalszh.exe", "data/ini/gamedata.ini", "data/ini/object.ini", "data/generals.str",
                     "data/english/language.ini", "window/menus/mainmenu.wnd", "window/controlbar.wnd",
                     "data/scripts/skirmishscripts.scb", "maps/ironwood_crossing/ironwood_crossing.map",
                     "data/ini/audiosettings.ini", "data/ini/playertemplate.ini", "data/ini/aidata.ini"):
            self.assertIn(path, self.files)

    def test_references_resolve(self):
        rc = validate_pack.main([self.out])
        self.assertEqual(rc, 0)

    def test_big_build_contains_the_same_files(self):
        out = os.path.join(self.tmp, "big")
        build_pack.main([out, "--big"])
        with open(os.path.join(out, "StarterPack.big"), "rb") as f:
            names = set(n.replace("\\", "/") for n in read_big(f.read()))
        self.assertEqual(names, set(self.files))


if __name__ == "__main__":
    unittest.main()
