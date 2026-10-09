"""The generated content: models, layouts, scripts and the map are structurally what the engine reads."""
import os
import re
import unittest

import _path  # noqa: F401
from spk.engine_schema import find_repo_root
from spk.mapfile import read_map, write_map
from spk.scripts import ACTIONS, CONDITIONS, PARAM
from spk.w3d import parse_w3d
from spk.wnd import parse_wnd


def capture(module):
    out = {}

    def emit(path, data):
        assert path.lower() not in {k.lower() for k in out}, "emitted twice: " + path
        out[path] = data if isinstance(data, bytes) else data.encode("latin-1")
    module.generate(emit)
    return out


class ModelTest(unittest.TestCase):
    def test_every_model_parses_and_is_named_like_its_file(self):
        from gen import models
        files = capture(models)
        w3d = {p: d for p, d in files.items() if p.endswith(".w3d")}
        self.assertGreaterEqual(len(w3d), 17)
        for path, data in w3d.items():
            info = parse_w3d(data)
            name = os.path.basename(path)[:-4]
            self.assertLess(len(name), 16, path)
            self.assertTrue(info["meshes"], path)
            self.assertIsNotNone(info["hlod"], path)
            self.assertEqual(info["hlod"]["name"], name, path)
        for engine_name in ("Locater01", "Locater02", "SCMNode", "new_skybox", "SPMoveHint"):
            self.assertIn("Art/W3D/%s.w3d" % engine_name, w3d)


class LayoutTest(unittest.TestCase):
    def test_layouts_parse_and_names_are_unique(self):
        from gen import wnd_menus, wnd_game
        for module in (wnd_menus, wnd_game):
            for path, data in capture(module).items():
                if not path.endswith(".wnd"):
                    continue
                layout, windows = parse_wnd(data)
                names = [w.name for top in windows for w in top.walk()]
                self.assertEqual(len(names), len(set(names)), "duplicate window names in " + path)
                self.assertIn("LAYOUTINIT", layout)

    def test_combo_boxes_are_drawn_last_lowest_first(self):
        """The list of a combo box opens downward over what is below it: windows drawn later sit on top, so every
        combo box must come after the other windows of its parent, from the bottom of the screen to the top."""
        from gen import wnd_extra, wnd_lan, wnd_menus
        checked = 0
        for module in (wnd_extra, wnd_lan, wnd_menus):
            for path, data in capture(module).items():
                if not path.endswith(".wnd"):
                    continue
                _layout, windows = parse_wnd(data)
                for top in windows:
                    for w in top.walk():
                        kinds = [c.kind for c in w.children]
                        if "COMBOBOX" not in kinds:
                            continue
                        first = kinds.index("COMBOBOX")
                        self.assertTrue(all(k == "COMBOBOX" for k in kinds[first:]), path + " " + w.name)
                        ys = [c.rect[1] for c in w.children[first:]]
                        self.assertEqual(ys, sorted(ys, reverse=True), path + " " + w.name)
                        checked += 1
        self.assertGreater(checked, 3)

    def test_check_box_clears_its_label(self):
        """W3DCheckBox.cpp: the box starts a sixteenth of the width in and is a third of the height wide, the label
        starts one height from the left edge; a window wider than 16 * (2/3 height - 4) would draw the box over it."""
        from gen.wnd_widgets import checkbox
        for width in (100, 224, 340, 1000):
            w = checkbox("C", (10, 100, 10 + width, 124), "GUI:X")
            x0, y0, x1, y1 = w.rect
            h = y1 - y0
            box_right = (x1 - x0) // 16 + h // 3
            self.assertLess(box_right, h, "box overlaps the label for width %d" % width)

    def test_main_menu_has_the_windows_the_engine_looks_up(self):
        from gen import wnd_menus
        files = capture(wnd_menus)
        _l, windows = parse_wnd(files["Window/Menus/MainMenu.wnd"])
        have = {w.name.split(":")[1] for top in windows for w in top.walk()}
        # MainMenuInput shows MapBorder2 on the first input; the menu code looks up the others by name
        for need in ("ButtonExit", "MapBorder", "MapBorder2", "MapBorder3", "MapBorder4"):
            self.assertIn(need, have)


class ScriptSchemaTest(unittest.TestCase):
    """The ids written into script chunks are positions in the engine's enums; check them against the sources."""

    @classmethod
    def setUpClass(cls):
        try:
            cls.repo = find_repo_root()
        except RuntimeError:
            cls.repo = None

    def enums(self):
        header = os.path.join(self.repo, "GeneralsMD/Code/GameEngine/Include/GameLogic/Scripts.h")
        with open(header, encoding="latin-1") as f:
            src = re.sub(r"//[^\n]*", "", f.read())

        def enum(name):
            body = re.search(r"enum " + name + r"\s*\{(.*?)\}", src, re.S).group(1)
            return [x.strip().split("=")[0].strip() for x in body.split(",") if x.strip()]
        return enum("ScriptActionType"), enum("ConditionType"), enum("ParameterType")

    def templates(self, kind):
        path = os.path.join(self.repo, "GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptEngine.cpp")
        with open(path, encoding="latin-1") as f:
            text = f.read()
        out = {}
        for m in re.finditer(r"&m_%sTemplates\[(?:ScriptAction|Condition)::(\w+)\];(.*?)(?=curTemplate = &m_|\Z)" % kind, text, re.S):
            body = m.group(2)
            params = re.findall(r"m_parameters\[\d+\]\s*=\s*Parameter::(\w+)", body)
            n = re.search(r"m_numParameters\s*=\s*(\d+)", body)
            out[m.group(1)] = (int(n.group(1)) if n else 0, params)
        return out

    def test_ids_and_parameter_lists_match_the_engine(self):
        if not self.repo:
            self.skipTest("engine sources not available")
        actions, conditions, params = self.enums()
        for table, order, kind in ((ACTIONS, actions, "action"), (CONDITIONS, conditions, "condition")):
            templates = self.templates(kind)
            for name, (ident, kinds) in table.items():
                self.assertEqual(order.index(name), ident, name)
                count, declared = templates[name]
                self.assertEqual(declared, kinds, name)
                self.assertEqual(count, len(kinds), name)
        for name, ident in PARAM.items():
            self.assertEqual(params.index(name), ident, name)


class MapTest(unittest.TestCase):
    def test_map_round_trip_and_invariants(self):
        from gen import maps
        m = maps.make_map()
        data = write_map(m)
        r = read_map(data)
        self.assertEqual(r["order"], ["HeightMapData", "BlendTileData", "WorldInfo", "SidesList", "ObjectsList",
                                      "PolygonTriggers", "GlobalLighting", "WaypointsList"])
        self.assertEqual((r["width"], r["height"], r["border"]), (160, 160, 16))
        self.assertEqual(len(r["heights"]), 160 * 160)
        self.assertEqual(len(r["tiles"]), 160 * 160)
        self.assertEqual(r["num_bitmap_tiles"], 16)
        self.assertTrue(all(0 <= (t >> 2) < 16 for t in r["tiles"]))
        waypoints = {o.props.get("waypointName") for o in r["objects"] if "waypointID" in o.props}
        self.assertTrue({"Player_1_Start", "Player_2_Start"} <= waypoints)
        ids = [o.props.get("waypointID") for o in r["objects"] if "waypointID" in o.props]
        self.assertEqual(len(ids), len(set(ids)))
        names = [d.get("playerName") for d, _b in r["sides"]]
        self.assertEqual(names, ["", "Civilian", "SkirmishIronwood"])
        self.assertEqual(len(r["scripts"]), len(r["sides"]))
        for o in r["objects"]:                           # everything stays inside the playable area
            self.assertTrue(0 <= o.x <= 1280 and 0 <= o.y <= 1280, o.name)
        # base plateaus are flat enough to build on
        for sx, sy in maps.START:
            ix, iy = int(sx / 10) + 16, int(sy / 10) + 16
            around = [r["heights"][(iy + dy) * 160 + ix + dx] for dx in range(-6, 7) for dy in range(-6, 7)]
            self.assertLessEqual(max(around) - min(around), 2)

    def test_map_cache_entry_matches_the_map(self):
        from gen import maps
        data = write_map(maps.make_map())
        ini = maps.map_cache_ini(data)
        self.assertIn("fileSize = %d" % len(data), ini)
        self.assertIn("fileCRC = %d" % maps.engine_crc(data), ini)
        self.assertIn("numPlayers = 2", ini)
        self.assertIn("MapCache maps_5Cironwood_5Fcrossing_5Cironwood_5Fcrossing_2Emap", ini)
        self.assertEqual(maps.engine_crc(b"\xff\x01"), ((0xFF << 1) + 1) & 0xFFFFFFFF)   # crc = crc*2 + byte + carry

    def test_start_positions_are_clear_of_scenery(self):
        from gen import maps
        m = maps.make_map()
        for sx, sy in maps.START:
            for o in m.objects:
                if o.name.startswith("*"):
                    continue
                self.assertGreater(((o.x - sx) ** 2 + (o.y - sy) ** 2) ** 0.5, 200, o.name)


if __name__ == "__main__":
    unittest.main()
