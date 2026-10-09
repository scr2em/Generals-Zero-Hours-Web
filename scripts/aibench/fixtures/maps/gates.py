#!/usr/bin/env python3
"""Bench-only map "Ironwood Gates": two start positions like Ironwood Crossing, but every base lies in a basin whose rim is a
cliff ring with four gaps, so that the ways into a base are few and distinct (original content, GPL-3.0-or-later).

    python3 gates.py OUTDIR

writes OUTDIR/maps/ironwood_gates/ironwood_gates.map, .tga and map.str, and OUTDIR/append/maps/mapcache.ini (the map cache entry
that the overlay mechanism of the bench (lib/overlay.mjs, edit type "generate") appends to the pack's own map cache).

Each base (the start waypoints are the same as in Ironwood Crossing) is a flat plateau of radius 270 inside a rim of cliffs
(radius 270 to 310, 46 height steps high: the pathfinder treats a height step of 16 or more between neighbours as a cliff). The rim
has four gaps, relative to the direction of the other base: a wide one (150 units) facing it, narrow ones (70 units) to the left
and to the right, and a narrow one behind the base. Both bases have the same shape turned by 180 degrees around the middle of the map.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "Content", "StarterPack"))
sys.path.insert(0, os.path.join(PACK, "tools"))
sys.path.insert(0, PACK)

from gen import maps as base                                   # the Ironwood Crossing generator: sides, lighting, preview
from spk.datachunk import Dict
from spk.mapfile import Map, MapObject, TextureClass, write_map
from spk.util import Rng, clamp, smoothstep

NAME = "ironwood_gates"
W, H, BORDER, CELL = base.W, base.H, base.BORDER, base.CELL
START = base.START
FLAT = base.FLAT
RIM_IN, RIM_OUT = 270.0, 310.0
RIM_H = FLAT + 46
GRASS, DIRT, ROCK, SAND = base.GRASS, base.DIRT, base.ROCK, base.SAND


def gaps_of(i):
    """(center angle, half width in radians at the rim) of the gaps of base i."""
    sx, sy = START[i]
    ox, oy = START[1 - i]
    toward = math.atan2(oy - sy, ox - sx)
    r = 0.5 * (RIM_IN + RIM_OUT)
    return [(toward, 75.0 / r), (toward + math.pi / 2, 35.0 / r), (toward - math.pi / 2, 35.0 / r), (toward + math.pi, 35.0 / r)]


GAPS = [gaps_of(0), gaps_of(1)]


def angdiff(a, b):
    d = (a - b + math.pi) % (2 * math.pi) - math.pi
    return abs(d)


def in_gap(i, ang):
    return any(angdiff(ang, c) < hw for c, hw in GAPS[i])


def outside_height(wx, wy):
    n = base._fbm(wx, wy, 420.0, 11)
    return 24 + (n - 0.5) * 34


def height_byte(ix, iy):
    wx, wy = base.world_of(ix, iy)
    h = outside_height(wx, wy)
    for i, (sx, sy) in enumerate(START):
        r = math.hypot(wx - sx, wy - sy)
        ang = math.atan2(wy - sy, wx - sx)
        if r < RIM_IN:
            return FLAT
        if r <= RIM_OUT:
            if in_gap(i, ang):
                # the gap floor rises from the plateau to the outside ground
                k = smoothstep(clamp((r - RIM_IN) / (RIM_OUT + 60.0 - RIM_IN), 0.0, 1.0))
                return int(round(FLAT + (outside_height(wx, wy) - FLAT) * k))
            return RIM_H
        if r < RIM_OUT + 60.0 and in_gap(i, ang):
            k = smoothstep(clamp((r - RIM_IN) / (RIM_OUT + 60.0 - RIM_IN), 0.0, 1.0))
            h = FLAT + (h - FLAT) * k
    return int(clamp(round(h), 4, 200))


def build_terrain(m):
    for iy in range(H):
        for ix in range(W):
            m.heights[iy * W + ix] = height_byte(ix, iy)
    for iy in range(H):
        for ix in range(W):
            wx, wy = base.world_of(ix + 0.5, iy + 0.5)
            near = min(math.hypot(wx - sx, wy - sy) for sx, sy in START)
            n1 = base._fbm(wx, wy, 150.0, 31)
            if RIM_IN - 20 < near < RIM_OUT + 15:
                cls = ROCK
            elif near < 210:
                cls = DIRT if near < 75 else (GRASS if n1 < 0.62 else DIRT)
            else:
                cls = DIRT if n1 > 0.64 else GRASS
            m.tiles[iy * W + ix] = m.tile_index(cls, ix, iy)


def mark_cliffs(m):
    """The cliff state bits: a cell is a cliff when its four corners differ by more than the pathfinder's slope limit (9.8 units;
    one height step is 10/16 units) - the same rule as the editor's, which the map file does not store when it is not asked to."""
    row = (W + 7) // 8
    bits = bytearray(row * H)
    hs = m.heights
    for iy in range(H - 1):
        for ix in range(W - 1):
            v = (hs[iy * W + ix], hs[iy * W + ix + 1], hs[(iy + 1) * W + ix], hs[(iy + 1) * W + ix + 1])
            if (max(v) - min(v)) * 10.0 / 16.0 > 9.8:
                bits[iy * row + (ix >> 3)] |= 1 << (ix & 7)
    m.cliffs = bits


def objects(m):
    rng = Rng(3031)

    def props(uid):
        return Dict(**{"originalOwner": "team", "uniqueID": uid, "objectInitialHealth": 100, "objectEnabled": True,
                       "objectIndestructible": False, "objectUnsellable": False, "objectPowered": True,
                       "objectRecruitableAI": True, "objectTargetable": False})

    count = [0]

    def add(name, x, y, angle=0.0, d=None):
        count[0] += 1
        m.objects.append(MapObject(name, x, y, 0.0, angle, 0, d if d is not None else props("%s %d" % (name, count[0]))))

    wp = [0]

    def waypoint(name, x, y):
        wp[0] += 1
        add("*Waypoints/Waypoint", x, y, 0.0, Dict(waypointID=wp[0], waypointName=name, uniqueID=name))

    for i, (sx, sy) in enumerate(START):
        waypoint("Player_%d_Start" % (i + 1), sx, sy)
    waypoint("InitialCameraPosition", START[0][0], START[0][1])
    waypoint("Center", 640.0, 640.0)

    placed = []
    attempts = 0
    while len(placed) < 70 and attempts < 4000:
        attempts += 1
        x, y = rng.uniform(40, 1240), rng.uniform(40, 1240)
        if any(math.hypot(x - sx, y - sy) < RIM_OUT + 70 for sx, sy in START) or abs(x - y) < 110:
            continue
        if any(math.hypot(x - px, y - py) < 40 for px, py in placed):
            continue
        placed.append((x, y))
        add("StarterTree" if rng.random() < 0.6 else "StarterPine", x, y, rng.uniform(0, 6.283))


def make_map():
    m = Map(W, H, BORDER)
    m.classes = [TextureClass(base.CLASS_NAMES[i], i * 4, 4, 2) for i in range(4)]
    m.world = Dict(mapName="MAP:StarterGates", weather=0)
    base.sides(m)
    build_terrain(m)
    mark_cliffs(m)
    objects(m)
    base.lighting(m)
    return m


MAP_STR = 'MAP:StarterGates\n"Ironwood Gates"\nEND\n'


def main(out):
    m = make_map()
    data = write_map(m)
    key = "maps\\%s\\%s.map" % (NAME, NAME)
    cache = ["", "MapCache %s" % base.quoted_printable(key),
             "  fileSize = %d" % len(data),
             "  fileCRC = %d" % base.engine_crc(data),
             "  timestampLo = 0",
             "  timestampHi = 0",
             "  isOfficial = yes",
             "  isMultiplayer = yes",
             "  numPlayers = %d" % len(START),
             "  extentMin = X:0.00 Y:0.00 Z:0.00",
             "  extentMax = X:%.2f Y:%.2f Z:0.00" % ((W - 2 * BORDER) * CELL, (H - 2 * BORDER) * CELL),
             "  nameLookupTag = MAP:StarterGates",
             "  InitialCameraPosition = X:%.2f Y:%.2f Z:0.00" % START[0]]
    for i, (sx, sy) in enumerate(START):
        cache.append("  Player_%d_Start = X:%.2f Y:%.2f Z:0.00" % (i + 1, sx, sy))
    cache += ["END", ""]
    folder = os.path.join(out, "maps", NAME)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, NAME + ".map"), "wb") as f:
        f.write(data)
    with open(os.path.join(folder, NAME + ".tga"), "wb") as f:
        f.write(base.preview(m).to_tga(alpha=False, rle=True))
    with open(os.path.join(folder, "map.str"), "w", newline="") as f:
        f.write(MAP_STR)
    os.makedirs(os.path.join(out, "append", "maps"), exist_ok=True)
    with open(os.path.join(out, "append", "maps", "mapcache.ini"), "w", newline="") as f:
        f.write("\n".join(cache))


if __name__ == "__main__":
    main(sys.argv[1])
