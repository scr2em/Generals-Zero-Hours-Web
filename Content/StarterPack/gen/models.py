"""The 3D art: the Ironwood Compact's buildings and units, and the scenery, built from primitives.

Every model uses one tiny palette texture (``sp_iw.tga``: 16 flat colours in a row) so a model is one mesh
plus, where the player's colour shows, a ``HOUSECOLOR`` mesh (the engine recolours the vertex material of
meshes whose name starts with HOUSECOLOR). Names are short (the W3D format limits a model name to 15 letters).
"""

import math

from spk.image import Canvas
from spk.util import Rng
from spk.w3d import Mesh, Model, Shader, VertexMaterial, Animation, quat_axis_angle

# palette cells of sp_iw.tga
HULL, DARK, LIGHT, AMBER, GLASS, RUBBER, SKIN, CLOTH, WOOD, LEAF, LEAF_DARK, ROCK, WHITE, RED, CONCRETE, OLIVE = range(16)
PALETTE = [
    (110, 130, 160), (58, 70, 90), (172, 192, 216), (232, 168, 48), (90, 150, 190), (30, 32, 36), (224, 184, 150),
    (110, 118, 86), (120, 84, 50), (64, 116, 58), (38, 80, 42), (120, 118, 112), (236, 240, 246), (200, 60, 50),
    (150, 148, 140), (84, 100, 64),
]
TEXTURE = "sp_iw.tga"


def palette_texture():
    c = Canvas(128, 8)
    rng = Rng(5)
    for i, col in enumerate(PALETTE):
        for y in range(8):
            for x in range(8):
                shade = 1.0 + (rng.random() - 0.5) * 0.08
                c.set(i * 8 + x, y, tuple(int(min(255, max(0, v * shade))) for v in col) + (255,))
    return c.to_tga(alpha=False, rle=True)


def _cell_uv(cell):
    return ((cell + 0.5) / 16.0, 0.5)


class Builder:
    """Collects painted primitives into the body mesh and the team colour mesh of one model."""

    def __init__(self, name):
        self.name = name
        self.body = Mesh("BODY", texture=TEXTURE, material=VertexMaterial(name="iw_body", specular=(20, 20, 20), shininess=8.0))
        self.team = Mesh("HOUSECOLOR", texture=None, shader=Shader.opaque(texturing=False),
                         material=VertexMaterial(name="iw_team", diffuse=(255, 255, 255), ambient=(255, 255, 255)))

    # -- placement helpers -------------------------------------------------------------------------
    def _add(self, mesh, builder, cell, translate, axis, rotate_z, target):
        tmp = Mesh("TMP")
        builder(tmp)
        tmp.uvs = [_cell_uv(cell)] * len(tmp.uvs) if cell is not None else tmp.uvs
        if axis == "y":      # the primitive's Z axis becomes the world Y axis
            tmp.positions = [(x, -z, y) for x, y, z in tmp.positions]
            tmp.normals = [(x, -z, y) for x, y, z in tmp.normals]
        elif axis == "x":    # the primitive's Z axis becomes the world X axis
            tmp.positions = [(z, y, -x) for x, y, z in tmp.positions]
            tmp.normals = [(z, y, -x) for x, y, z in tmp.normals]
        target.append(tmp, translate=translate, rotate_z=rotate_z)

    def box(self, center, size, cell, taper=None, rotate_z=0.0, team=False):
        self._add(None, lambda m: m.add_box((0, 0, 0), size, taper=taper), None if team else cell,
                  center, "z", rotate_z, self.team if team else self.body)

    def prism(self, center, radius, height, cell, sides=8, radius_top=None, axis="z", team=False):
        self._add(None, lambda m: m.add_prism((0, 0, 0), radius, height, sides=sides, radius_top=radius_top),
                  None if team else cell, center, axis, 0.0, self.team if team else self.body)

    def sphere(self, center, radius, cell, squash=(1, 1, 1), team=False):
        self._add(None, lambda m: m.add_sphere((0, 0, 0), radius, segments=8, rings=5, squash=squash),
                  None if team else cell, center, "z", 0.0, self.team if team else self.body)

    def wedge(self, center, size, cell, rotate_z=0.0):
        self._add(None, lambda m: m.add_wedge((0, 0, 0), size), cell, center, "z", rotate_z, self.body)

    # -- result ---------------------------------------------------------------------------------------
    def model(self):
        model = Model(self.name)
        if self.body.triangles:
            model.add_mesh(self.body)
        if self.team.triangles:
            model.add_mesh(self.team)
        return model


# --------------------------------------------------------------------------------------------------
# structures
# --------------------------------------------------------------------------------------------------

def hq():
    b = Builder("iw_hq")
    b.box((0, 0, 2), (76, 76, 4), CONCRETE)                                   # platform
    b.box((0, 0, 11), (60, 60, 14), HULL)                                     # main block
    b.box((0, 0, 19), (62, 62, 2), DARK)                                      # roof rim
    b.box((0, -31, 10), (20, 2, 10), DARK)                                    # gate
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.prism((sx * 28, sy * 28, 14), 3.5, 20, LIGHT, sides=6)         # corner pillars
            b.box((sx * 28, sy * 28, 25), (8, 8, 2), DARK)
    b.box((0, 4, 25), (26, 26, 12), LIGHT)                                    # command tower
    b.box((0, 4, 32), (28, 28, 2), DARK)
    b.sphere((0, 4, 33), 9, GLASS, squash=(1, 1, 0.85))                       # glass dome
    b.prism((10, 4, 40), 0.6, 14, WHITE, sides=4)                             # antenna
    b.box((0, 4, 38), (3, 1.2, 2), AMBER)
    for x in (-20, -10, 10, 20):
        b.box((x, -30.4, 14), (5, 1, 3), AMBER)                               # windows
    b.box((-22, -31, 14), (8, 1.2, 12), 0, team=True)                         # banner in the player's colour
    b.box((22, -31, 14), (8, 1.2, 12), 0, team=True)
    return b.model()


def power():
    b = Builder("iw_power")
    b.box((0, 0, 1.5), (40, 40, 3), CONCRETE)
    b.prism((0, 0, 8), 16, 10, HULL, sides=10)                                # reactor drum
    b.prism((0, 0, 14), 16.5, 2, DARK, sides=10)
    b.prism((-9, 0, 22), 7, 22, LIGHT, sides=10, radius_top=5)                # cooling towers
    b.prism((9, 0, 22), 7, 22, LIGHT, sides=10, radius_top=5)
    b.prism((-9, 0, 34), 5, 1.5, DARK, sides=10)
    b.prism((9, 0, 34), 5, 1.5, DARK, sides=10)
    b.sphere((0, -10, 17), 4, AMBER, squash=(1, 1, 0.8))                      # glowing core
    b.box((0, -17, 6), (10, 4, 6), DARK)
    b.prism((0, 0, 9), 16.6, 3, 0, sides=10, team=True)                       # player colour band
    return b.model()


def barracks():
    b = Builder("iw_barracks")
    b.box((0, 0, 1.5), (62, 42, 3), CONCRETE)
    b.box((0, 0, 9), (56, 34, 12), CLOTH)                                     # hall
    b.wedge((0, 0, 17), (58, 36, 4), DARK)                                    # roof (ridge at the back)
    b.box((0, -17.5, 8), (12, 1.5, 9), DARK)                                  # door
    for x in (-18, -8, 8, 18):
        b.box((x, -17.2, 10), (5, 1, 4), GLASS)
    b.prism((24, -14, 24), 0.7, 14, WHITE, sides=4)                           # flag pole
    b.box((27.5, -14, 28), (7, 0.5, 4), 0, team=True)                         # pennant
    b.box((-24, -17.5, 8), (6, 1.4, 10), 0, team=True)
    return b.model()


def factory():
    b = Builder("iw_factory")
    b.box((0, 0, 1.5), (76, 60, 3), CONCRETE)
    b.box((0, 0, 10), (72, 56, 14), HULL)
    for i in range(3):                                                        # saw tooth roof
        b.wedge((-24 + i * 24, 0, 20), (24, 54, 6), DARK)
    b.box((0, -28.5, 8), (30, 2, 12), DARK)                                   # roller door
    for k in range(4):
        b.box((0, -29.6, 4 + k * 3), (28, 0.4, 0.8), LIGHT)
    b.prism((28, 18, 26), 3, 20, LIGHT, sides=8, radius_top=2.4)              # chimney
    b.box((-28, -28.5, 12), (6, 2, 14), 0, team=True)
    b.box((28, -28.5, 12), (6, 2, 14), 0, team=True)
    b.box((0, -28.6, 18.5), (66, 1.4, 1.6), AMBER)
    return b.model()


# --------------------------------------------------------------------------------------------------
# infantry
# --------------------------------------------------------------------------------------------------

def person(name, torso, helmet, weapon=None, tool=None):
    b = Builder(name)
    for sx in (-1, 1):
        b.box((sx * 1.6, 0, 2.6), (2.4, 2.8, 5.2), DARK)                    # legs
        b.box((sx * 1.6, 0.4, 0.4), (2.6, 4.2, 0.9), RUBBER)                # boots
    b.box((0, 0, 7.8), (6.4, 3.6, 5.6), torso)                                # torso
    b.box((0, -2.05, 8.2), (5.2, 0.5, 4.2), 0, team=True)                     # vest in the player's colour
    for sx in (-1, 1):
        b.box((sx * 4.0, 0, 7.6), (1.8, 2.2, 5.0), torso)                   # arms
    b.sphere((0, 0, 12.4), 2.1, SKIN)                                         # head
    b.sphere((0, 0, 13.0), 2.35, helmet, squash=(1, 1, 0.8))                  # helmet
    if weapon == "rifle":
        b.box((3.4, -2.4, 8.2), (0.8, 7.0, 0.9), RUBBER)
        b.box((3.4, -2.8, 7.6), (0.9, 2.0, 1.4), WOOD)
    elif weapon == "rocket":
        b.prism((3.8, -1.0, 11.2), 1.1, 8.5, DARK, sides=6, axis="y")
        b.prism((3.8, -5.3, 11.2), 1.5, 0.8, LIGHT, sides=6, axis="y")
    if tool:
        b.box((4.4, -2.2, 8.4), (0.8, 0.8, 5.6), LIGHT)
        b.box((4.4, -2.2, 11.4), (2.4, 0.8, 1.0), LIGHT)
    return b.model()


def worker():
    return person("iw_worker", AMBER, AMBER, tool=True)


def rifleman():
    return person("iw_rifleman", CLOTH, OLIVE, weapon="rifle")


def rocketeer():
    return person("iw_rocketeer", CLOTH, HULL, weapon="rocket")


# --------------------------------------------------------------------------------------------------
# vehicles
# --------------------------------------------------------------------------------------------------

def wheel(b, x, y, r=3.4):
    b.prism((x, y, r), r, 2.4, RUBBER, sides=10, axis="x")
    b.prism((x + (1.3 if x > 0 else -1.3), y, r), r * 0.5, 0.4, LIGHT, sides=8, axis="x")


def scout():
    b = Builder("iw_scout")
    b.box((0, 0, 5.4), (11, 22, 3.6), HULL)                                   # chassis
    b.box((0, -2, 8.6), (9, 9, 3.2), GLASS, taper=(0.8, 0.7))                 # cab
    b.box((0, 6.5, 8), (8, 6, 2), HULL)
    b.box((0, -9.8, 6.2), (8.6, 2, 1.8), DARK)                                # front bumper
    for y in (-7.5, 7.5):
        for sx in (-1, 1):
            wheel(b, sx * 6.2, y)
    b.prism((0, 6, 11), 0.8, 3, DARK, sides=6)                                # gun mount
    b.box((0, 2.5, 12.2), (1.2, 7, 1.2), RUBBER)                              # machine gun
    b.box((0, 0.5, 5.8), (11.4, 6, 0.8), 0, team=True)                        # player colour stripe
    return b.model()


def tank():
    b = Builder("iw_tank")
    for sx in (-1, 1):
        b.box((sx * 8.2, 0, 3.2), (4.6, 28, 6.4), RUBBER)                    # treads
        b.box((sx * 8.2, 0, 3.4), (3.6, 26, 6.8), DARK, taper=(0.9, 0.96))
        for k in range(5):
            b.prism((sx * 10.6, -10 + k * 5, 3.2), 2.2, 0.8, LIGHT, sides=8, axis="x")
    b.box((0, 0, 6.2), (13, 27, 4.4), HULL, taper=(0.92, 0.9))                # hull
    b.box((0, -13.8, 6.4), (11, 1.6, 3.6), DARK)
    b.box((0, 0, 9.6), (11.6, 12.6, 3.4), LIGHT, taper=(0.8, 0.84))           # turret
    b.sphere((0, 1.5, 11.4), 2.4, DARK, squash=(1, 1, 0.5))                   # hatch
    b.box((0, -12, 10.2), (1.6, 15, 1.6), DARK)                               # barrel
    b.box((0, -19.5, 10.2), (2.4, 2.4, 2.4), RUBBER)                          # muzzle brake
    b.box((0, 0, 11.4), (11.8, 4, 0.8), 0, team=True)                         # turret stripe
    return b.model()


# --------------------------------------------------------------------------------------------------
# scenery
# --------------------------------------------------------------------------------------------------

def tree():
    b = Builder("sp_tree")
    b.prism((0, 0, 4), 1.8, 8, WOOD, sides=6, radius_top=1.4)
    b.sphere((0, 0, 14), 7.5, LEAF, squash=(1, 1, 1.15))
    b.sphere((3.2, 1.5, 11), 4.6, LEAF_DARK, squash=(1, 1, 0.9))
    b.sphere((-3, -1.5, 18), 4.2, LEAF, squash=(1, 1, 0.9))
    return b.model()


def pine():
    b = Builder("sp_pine")
    b.prism((0, 0, 2.5), 1.5, 5, WOOD, sides=6)
    for k, (r, z) in enumerate(((8, 8), (6.4, 14), (4.6, 20), (3, 26))):
        b.prism((0, 0, z), r, 8, LEAF_DARK if k % 2 == 0 else LEAF, sides=8, radius_top=0.2, cap_bottom=False) \
            if False else b.prism((0, 0, z), r, 8, LEAF_DARK if k % 2 == 0 else LEAF, sides=8, radius_top=0.4)
    return b.model()


def rock():
    b = Builder("sp_rock")
    b.sphere((0, 0, 4.5), 9, ROCK, squash=(1.1, 0.9, 0.62))
    b.sphere((6, 3, 3), 5.5, CONCRETE, squash=(1, 1, 0.7))
    b.sphere((-5, -4, 2.6), 4.2, DARK, squash=(1, 1, 0.7))
    return b.model()


def flag():
    b = Builder("sp_flag")
    b.prism((0, 0, 5), 0.5, 10, WHITE, sides=4)
    b.box((2.6, 0, 8.2), (5, 0.3, 3), 0, team=True)
    return b.model()


# --------------------------------------------------------------------------------------------------
# models the engine loads by fixed name (InGameUI, waypoint drawing, the water code)
# --------------------------------------------------------------------------------------------------

def locater01():
    """The anchor that marks where a building will stand while its facing is being chosen."""
    b = Builder("Locater01")
    b.prism((0, 0, 0.6), 12, 1.2, WHITE, sides=16)
    b.prism((0, 0, 1.4), 9, 0.8, AMBER, sides=16)
    b.prism((0, 0, 6), 1.0, 12, AMBER, sides=4)
    return b.model()


def locater02():
    """The arrow that shows that facing."""
    b = Builder("Locater02")
    b.box((10, 0, 0.6), (20, 3, 1.2), WHITE)
    b.wedge((24, 0, 0.6), (10, 12, 1.2), AMBER, rotate_z=-1.5708)
    return b.model()


def scmnode():
    """A waypoint node, drawn while a path is shown."""
    b = Builder("SCMNode")
    b.sphere((0, 0, 3), 3, AMBER)
    b.prism((0, 0, 0.5), 3.5, 1, WHITE, sides=8)
    return b.model()


def movehint():
    """The marker that flashes where a move order was given."""
    b = Builder("SPMoveHint")
    b.prism((0, 0, 0.5), 8, 1.0, LEAF, sides=12)
    b.prism((0, 0, 1.2), 5.5, 0.8, LEAF_DARK, sides=12)
    b.prism((0, 0, 3), 1.2, 6, LEAF, sides=4, radius_top=0.2)
    return b.model()


def skybox():
    """Five inward facing faces textured with the sky picture (the water code draws it when a map asks for it)."""
    m = Mesh("SKYBOX", texture="swsky.tga", shader=Shader.opaque(),
             material=VertexMaterial(name="iw_sky", diffuse=(255, 255, 255), ambient=(255, 255, 255)))
    r, z0, z1 = 100.0, -20.0, 120.0
    lo, hi = -r, r
    c = [(lo, lo, z0), (hi, lo, z0), (hi, hi, z0), (lo, hi, z0), (lo, lo, z1), (hi, lo, z1), (hi, hi, z1), (lo, hi, z1)]
    uv = ((0, 1), (1, 1), (1, 0), (0, 0))
    for a, b_, c_, d in ((0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        m.add_quad(c[d], c[c_], c[b_], c[a], uv)          # reversed winding: faces the inside
    m.add_quad(c[7], c[6], c[5], c[4], uv)
    model = Model("new_skybox")
    model.add_mesh(m)
    return model


def models():
    return [hq(), power(), barracks(), factory(), worker(), rifleman(), rocketeer(), scout(), tank(), tree(), pine(),
            rock(), flag(), locater01(), locater02(), scmnode(), movehint(), skybox()]


# --------------------------------------------------------------------------------------------------
# animations: a walk cycle for the infantry would need bones; the first release moves them rigidly.
# --------------------------------------------------------------------------------------------------

def generate(emit):
    emit("Art/Textures/sp_iw.tga", palette_texture())
    for m in models():
        emit("Art/W3D/%s.w3d" % m.name, m.to_bytes())
