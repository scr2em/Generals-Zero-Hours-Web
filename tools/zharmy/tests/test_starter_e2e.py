"""End to end on our own free starter pack: build it, convert its faction, validate the result.

The starter pack builder (Content/StarterPack/build_pack.py) takes about 20 seconds; the test is skipped when the
pack sources are not next to the tools folder.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from . import _path  # noqa: F401
from zharmy import package, validate as validatemod, w3d
from zharmy.convert import Options, convert
from zharmy.inspect_mod import inspect_factions

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
PACK_SRC = os.path.join(ROOT, "Content", "StarterPack")
PACK_ENV = os.environ.get("ZHARMY_STARTER_PACK")      # an already built pack folder


@unittest.skipUnless(PACK_ENV or os.path.exists(os.path.join(PACK_SRC, "build_pack.py")), "starter pack sources missing")
class StarterEndToEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="zharmy-starter-")
        if PACK_ENV:
            cls.pack = PACK_ENV
        else:
            subprocess.check_call([sys.executable, "build_pack.py", cls.tmp], cwd=PACK_SRC,
                                  stdout=subprocess.DEVNULL)
            cls.pack = os.path.join(cls.tmp, "StarterPack")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_inspect(self):
        res = inspect_factions([self.pack])
        self.assertEqual([f["playerTemplate"] for f in res["factions"]], ["FactionIronwood"])
        self.assertTrue(res["factions"][0]["skirmishBuildList"] and res["factions"][0]["skirmishScripts"])
        self.assertEqual(res["factions"][0]["displayName"], "Ironwood Compact")

    def test_convert_requires_starter(self):
        out = os.path.join(self.tmp, "iw.zharmy")
        c, rep = convert([self.pack], [self.pack], Options(tag="IWD", faction="FactionIronwood", requires="starter"), out)
        res = validatemod.validate(out, [self.pack])
        self.assertTrue(res.ok, res.errors)
        pkg = package.Package(out)
        self.assertEqual(pkg.manifest()["requires"], ["starter"])
        self.assertIn("IronwoodHQ", rep["definitionsCopied"]["Object"])
        self.assertIn("StarterEmptyCommandSet", rep["definitionsKeptAsReferences"]["CommandSet"])
        self.assertTrue(pkg.manifest()["factions"][0]["ai"])

    def test_convert_self_contained(self):
        out = os.path.join(self.tmp, "iwn.zharmy")
        c, rep = convert([self.pack], [], Options(tag="IWN", faction="FactionIronwood", requires="none"), out)
        res = validatemod.validate(out)
        self.assertTrue(res.ok, res.errors)
        pkg = package.Package(out)
        models = [n for n in pkg.names() if n.startswith("art/w3d/")]
        self.assertEqual(len(models), 9)                       # 9 units and buildings
        for n in models:
            sc = w3d.scan(w3d.parse(pkg.read(n)))
            self.assertTrue(all(d.startswith("iwn") for d in sc.defined))
        # converting again gives the same bytes
        out2 = os.path.join(self.tmp, "iwn2.zharmy")
        convert([self.pack], [], Options(tag="IWN", faction="FactionIronwood", requires="none"), out2)
        with open(out, "rb") as a, open(out2, "rb") as b:
            self.assertEqual(a.read(), b.read())


if __name__ == "__main__":
    unittest.main()
