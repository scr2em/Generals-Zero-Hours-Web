"""The two player skirmish map "Ironwood Crossing" and the files that belong to it.

    Maps/ironwood_crossing/ironwood_crossing.map    terrain, objects, sides, waypoints (spk.mapfile)
    Maps/ironwood_crossing/ironwood_crossing.tga    the preview picture the setup screen shows
    Maps/ironwood_crossing/map.str                  the display name (MAP:StarterCrossing)

The map is 160 x 160 cells of 10 units with a 16 cell border, so the playable area is 1280 x 1280 units. The two
start waypoints sit in opposite corners on flat ground with a rolling ridge between them. The scripts for the
computer opponent come from ``Data/Scripts/SkirmishScripts.scb`` (gen/ai_scripts.py), the same way the shipped
skirmish maps share theirs, so the map itself carries empty script lists.
"""

import math

from spk.datachunk import Dict, Unicode
from spk.image import Canvas
from spk.mapfile import (Map, MapObject, TextureClass, Light, BuildEntry, TIME_MORNING, TIME_AFTERNOON, TIME_EVENING,
                         TIME_NIGHT, write_map)
from spk.scripts import ScriptList
from spk.util import Rng, hash_noise_2d, clamp, smoothstep

NAME = "ironwood_crossing"
W = H = 160
BORDER = 16
CELL = 10.0

START = [(280.0, 280.0), (1000.0, 1000.0)]       # world units (inside the playable area)
FLAT = 26                                         # height byte of the base plateaus

GRASS, DIRT, ROCK, SAND = range(4)
CLASS_NAMES = ["SwGrass", "SwDirt", "SwRock", "SwSand"]


def _noise(x, y, scale, seed):
    """Smooth value noise in 0..1."""
    u, v = x / scale, y / scale
    x0, y0 = math.floor(u), math.floor(v)
    fx, fy = smoothstep(u - x0), smoothstep(v - y0)
    a, b = hash_noise_2d(x0, y0, seed), hash_noise_2d(x0 + 1, y0, seed)
    c, d = hash_noise_2d(x0, y0 + 1, seed), hash_noise_2d(x0 + 1, y0 + 1, seed)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def _fbm(x, y, scale, seed):
    return (_noise(x, y, scale, seed) * 0.6 + _noise(x, y, scale / 2.3, seed + 1) * 0.28
            + _noise(x, y, scale / 5.1, seed + 2) * 0.12)


def world_of(ix, iy):
    """World position of the terrain vertex (ix, iy)."""
    return (ix - BORDER) * CELL, (iy - BORDER) * CELL


def height_byte(ix, iy):
    wx, wy = world_of(ix, iy)
    n = _fbm(wx, wy, 420.0, 11)
    h = 24 + (n - 0.5) * 70
    d = abs((wx - wy)) / 1.4142                      # distance from the diagonal between the start corners
    # hills away from the diagonal that joins the bases, so the way between them stays open
    h += clamp((d - 330.0) / 300.0, 0, 1) * 20 * (0.5 + _noise(wx, wy, 260.0, 21))
    # flatten the base plateaus
    for sx, sy in START:
        r = math.hypot(wx - sx, wy - sy)
        k = 1.0 - smoothstep(clamp((r - 190.0) / 190.0, 0.0, 1.0))
        h = h + (FLAT - h) * k
    return int(clamp(round(h), 4, 200))


def build_terrain(m):
    for iy in range(H):
        for ix in range(W):
            m.heights[iy * W + ix] = height_byte(ix, iy)
    steep = {}
    for iy in range(H):
        for ix in range(W):
            hs = m.heights
            h0 = hs[iy * W + ix]
            h1 = hs[iy * W + min(W - 1, ix + 1)]
            h2 = hs[min(H - 1, iy + 1) * W + ix]
            steep[(ix, iy)] = max(abs(h0 - h1), abs(h0 - h2))
    for iy in range(H):
        for ix in range(W):
            wx, wy = world_of(ix + 0.5, iy + 0.5)
            h = m.heights[iy * W + ix]
            near = min(math.hypot(wx - sx, wy - sy) for sx, sy in START)
            n1 = _fbm(wx, wy, 150.0, 31)
            n2 = _fbm(wx, wy, 190.0, 41)
            cls = GRASS
            if near < 75:
                cls = DIRT
            elif near < 210:
                cls = GRASS if n1 < 0.62 else DIRT
            else:
                if n1 > 0.64:
                    cls = DIRT
                if n2 > 0.66 and h < 30:
                    cls = SAND
                if h > 52 or steep[(ix, iy)] > 3:
                    cls = ROCK
            m.tiles[iy * W + ix] = m.tile_index(cls, ix, iy)


def objects(m):
    rng = Rng(2024)
    props = lambda owner="team", uid="": Dict(**{
        "originalOwner": owner, "uniqueID": uid, "objectInitialHealth": 100, "objectEnabled": True,
        "objectIndestructible": False, "objectUnsellable": False, "objectPowered": True,
        "objectRecruitableAI": True, "objectTargetable": False})
    counter = [0]

    def add(name, x, y, angle=0.0, d=None):
        counter[0] += 1
        m.objects.append(MapObject(name, x, y, 0.0, angle, 0, d if d is not None else props(uid="%s %d" % (name, counter[0]))))

    # the start positions and a couple of rally waypoints
    wp = []

    def waypoint(name, x, y):
        wp.append(len(wp) + 1)
        d = Dict(waypointID=len(wp), waypointName=name, uniqueID=name)
        add("*Waypoints/Waypoint", x, y, 0.0, d)

    for i, (sx, sy) in enumerate(START):
        waypoint("Player_%d_Start" % (i + 1), sx, sy)
    waypoint("InitialCameraPosition", START[0][0], START[0][1])
    waypoint("Center", 640.0, 640.0)

    placed = []

    def free(x, y, r):
        if not (60 < x < 1220 and 60 < y < 1220):
            return False
        for sx, sy in START:
            if math.hypot(x - sx, y - sy) < 230:
                return False
        if abs(x - y) < 90:      # keep the diagonal between the bases open
            return False
        for px, py, pr in placed:
            if math.hypot(x - px, y - py) < r + pr:
                return False
        return True

    attempts = 0
    while len([1 for o in m.objects if o.name != "*Waypoints/Waypoint"]) < 150 and attempts < 6000:
        attempts += 1
        x, y = rng.uniform(40, 1240), rng.uniform(40, 1240)
        density = _fbm(x, y, 260.0, 77)
        if density < 0.5 and rng.random() < 0.8:
            continue
        iy, ix = int(y / CELL) + BORDER, int(x / CELL) + BORDER
        tile_cls = (m.tiles[iy * W + ix] >> 2) // 4
        if tile_cls == ROCK:
            name, r = ("StarterRock", 16) if rng.random() < 0.7 else ("StarterPine", 10)
        elif tile_cls == SAND:
            if rng.random() < 0.7:
                continue
            name, r = "StarterRock", 16
        else:
            name, r = ("StarterTree", 11) if _noise(x, y, 200.0, 5) < 0.55 else ("StarterPine", 10)
        if not free(x, y, r):
            continue
        placed.append((x, y, r))
        add(name, x, y, rng.uniform(0, 6.283))


def sides(m):
    def side(name, faction, human=False, display=""):
        d = Dict()
        d.set("playerName", name)
        d.set("playerIsHuman", human)
        d.set("playerDisplayName", Unicode(display or name))
        d.set("playerFaction", faction)
        d.set("playerAllies", "")
        d.set("playerEnemies", "")
        return d
    m.sides = [
        (side("", "", False, "Neutral"), []),
        (side("Civilian", "FactionCivilian"), []),
        (side("SkirmishIronwood", "FactionIronwood"), []),
    ]
    for name, owner in (("team", ""), ("teamCivilian", "Civilian"), ("teamSkirmishIronwood", "SkirmishIronwood")):
        m.teams.append(Dict(teamName=name, teamOwner=owner, teamIsSingleton=True))
    m.scripts = [ScriptList() for _ in m.sides]


def lighting(m):
    sets = {
        TIME_MORNING: ((0.40, 0.40, 0.46), (0.75, 0.68, 0.60), (-0.6, 0.3, -0.75)),
        TIME_AFTERNOON: ((0.46, 0.46, 0.50), (0.88, 0.84, 0.74), (-0.5, -0.4, -0.76)),
        TIME_EVENING: ((0.38, 0.34, 0.42), (0.80, 0.55, 0.40), (0.6, -0.3, -0.74)),
        TIME_NIGHT: ((0.22, 0.24, 0.34), (0.30, 0.34, 0.50), (0.3, 0.5, -0.8)),
    }
    for tod, (amb, dif, direction) in sets.items():
        terrain = [Light(amb, dif, direction), Light(), Light()]
        objs = [Light(amb, dif, direction), Light(), Light()]
        m.lighting[tod] = {"terrain": terrain, "objects": objs}


def make_map():
    m = Map(W, H, BORDER)
    m.classes = [TextureClass(CLASS_NAMES[i], i * 4, 4, 2) for i in range(4)]
    m.world = Dict(mapName="MAP:StarterCrossing", weather=0)
    sides(m)
    build_terrain(m)
    objects(m)
    lighting(m)
    return m


def preview(m):
    """A 128 x 128 overview: terrain colours by class, shaded by height, with the start points marked."""
    colors = {GRASS: (74, 128, 58), DIRT: (150, 116, 78), ROCK: (122, 120, 114), SAND: (208, 188, 134)}
    size = 128
    c = Canvas(size, size, (0, 0, 0, 255))
    for py in range(size):
        for px in range(size):
            ix = BORDER + int(px * (W - 2 * BORDER) / size)
            iy = BORDER + int((size - 1 - py) * (H - 2 * BORDER) / size)
            t = m.tiles[iy * W + ix]
            col = colors[(t >> 2) // 4]
            hv = m.heights[iy * W + ix]
            f = 0.72 + 0.011 * (hv - 20)
            c.set(px, py, (int(clamp(col[0] * f, 0, 255)), int(clamp(col[1] * f, 0, 255)), int(clamp(col[2] * f, 0, 255)), 255))
    for i, (sx, sy) in enumerate(START):
        px = int(sx / 1280.0 * size)
        py = size - 1 - int(sy / 1280.0 * size)
        c.circle(px, py, 5, (255, 255, 255, 255), filled=True)
        c.circle(px, py, 3, ((70, 130, 210, 255) if i == 0 else (210, 90, 70, 255)), filled=True)
    c.frame(0, 0, size, size, (20, 20, 20, 255), 1)
    return c


MAP_STR = """MAP:StarterCrossing
"Ironwood Crossing"
END
"""


def generate(emit):
    m = make_map()
    base = "Maps/%s/%s" % (NAME, NAME)
    emit(base + ".map", write_map(m))
    emit(base + ".tga", preview(m).to_tga(alpha=False, rle=True))
    emit("Maps/%s/map.str" % NAME, MAP_STR)
