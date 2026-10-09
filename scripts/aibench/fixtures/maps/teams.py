#!/usr/bin/env python3
"""Bench-only map "Ironwood Teams": six start positions in two team areas, for team games (original content, GPL-3.0-or-later).

    python3 teams.py OUTDIR

writes OUTDIR/maps/ironwood_teams/ironwood_teams.map, .tga and map.str, and OUTDIR/append/maps/mapcache.ini (the map cache entry
that the overlay mechanism of the bench (lib/overlay.mjs, edit type "generate") appends to the pack's own map cache).

The map is 256 x 256 cells of 10 units with a 16 cell border (2240 x 2240 playable units). Starts 1, 2 and 3 are on the left edge, 600
units apart (one team area), starts 4, 5 and 6 are the same turned by 180 degrees around the middle of the map (the other team area):
start 4 faces start 3, start 5 faces start 2, start 6 faces start 1 across the open middle. Every start lies on a flat plateau of radius
330. No cliffs: the ground between the bases is open, a little rolling.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "Content", "StarterPack"))
sys.path.insert(0, os.path.join(PACK, "tools"))
sys.path.insert(0, PACK)

from gen import maps as base                                   # the Ironwood Crossing generator: sides, lighting, helpers
from spk.datachunk import Dict
from spk.mapfile import Map, MapObject, TextureClass, write_map
from spk.util import Rng, clamp, smoothstep

NAME = "ironwood_teams"
W = H = 256
BORDER, CELL = base.BORDER, base.CELL
SIZE = (W - 2 * BORDER) * CELL          # 2240
LEFT = [(300.0, 520.0), (300.0, 1120.0), (300.0, 1720.0)]
START = LEFT + [(SIZE - x, SIZE - y) for x, y in LEFT]
FLAT = base.FLAT
GRASS, DIRT, ROCK, SAND = base.GRASS, base.DIRT, base.ROCK, base.SAND


def world_of(ix, iy):
    return (ix - BORDER) * CELL, (iy - BORDER) * CELL


def height_byte(ix, iy):
    wx, wy = world_of(ix, iy)
    n = base._fbm(wx, wy, 520.0, 11)
    h = 24 + (n - 0.5) * 36
    for sx, sy in START:
        r = math.hypot(wx - sx, wy - sy)
        k = 1.0 - smoothstep(clamp((r - 330.0) / 220.0, 0.0, 1.0))
        h = h + (FLAT - h) * k
    return int(clamp(round(h), 4, 200))


def build_terrain(m):
    for iy in range(H):
        for ix in range(W):
            m.heights[iy * W + ix] = height_byte(ix, iy)
    for iy in range(H):
        for ix in range(W):
            wx, wy = world_of(ix + 0.5, iy + 0.5)
            near = min(math.hypot(wx - sx, wy - sy) for sx, sy in START)
            n1 = base._fbm(wx, wy, 150.0, 31)
            if near < 80:
                cls = DIRT
            elif near < 330:
                cls = GRASS if n1 < 0.62 else DIRT
            else:
                cls = DIRT if n1 > 0.64 else GRASS
            m.tiles[iy * W + ix] = m.tile_index(cls, ix, iy)


def objects(m):
    rng = Rng(4041)

    def props(uid):
        return Dict(**{"originalOwner": "team", "uniqueID": uid, "objectInitialHealth": 100, "objectEnabled": True,
                       "objectIndestructible": False, "objectUnsellable": False, "objectPowered": True,
                       "objectRecruitableAI": True, "objectTargetable": False})

    count = [0]

    def add(name, x, y, angle=0.0, d=None):
        count[0] += 1
        m.objects.append(MapObject(name, x, y, 0.0, angle, 0, d if d is not None else props("%s %d" % (name, count[0]))))

    for i, (sx, sy) in enumerate(START):
        add("*Waypoints/Waypoint", sx, sy, 0.0, Dict(waypointID=i + 1, waypointName="Player_%d_Start" % (i + 1), uniqueID="Player_%d_Start" % (i + 1)))
    add("*Waypoints/Waypoint", START[0][0], START[0][1], 0.0, Dict(waypointID=7, waypointName="InitialCameraPosition", uniqueID="InitialCameraPosition"))
    add("*Waypoints/Waypoint", SIZE / 2, SIZE / 2, 0.0, Dict(waypointID=8, waypointName="Center", uniqueID="Center"))

    placed = []
    attempts = 0
    while len(placed) < 160 and attempts < 8000:
        attempts += 1
        x, y = rng.uniform(40, SIZE - 40), rng.uniform(40, SIZE - 40)
        if any(math.hypot(x - sx, y - sy) < 330 for sx, sy in START):
            continue
        if any(math.hypot(x - px, y - py) < 40 for px, py in placed):
            continue
        placed.append((x, y))
        add("StarterTree" if rng.random() < 0.6 else "StarterPine", x, y, rng.uniform(0, 6.283))


def make_map():
    m = Map(W, H, BORDER)
    m.classes = [TextureClass(base.CLASS_NAMES[i], i * 4, 4, 2) for i in range(4)]
    m.world = Dict(mapName="MAP:StarterTeams", weather=0)
    base.sides(m)
    build_terrain(m)
    objects(m)
    base.lighting(m)
    return m


MAP_STR = 'MAP:StarterTeams\n"Ironwood Teams"\nEND\n'


def preview(m):
    from spk.image import Canvas
    size = 128
    c = Canvas(size, size, (0, 0, 0, 255))
    for py in range(size):
        for px in range(size):
            ix = BORDER + int(px * (W - 2 * BORDER) / size)
            iy = BORDER + int((size - 1 - py) * (H - 2 * BORDER) / size)
            hv = m.heights[iy * W + ix]
            f = 0.72 + 0.011 * (hv - 20)
            c.set(px, py, (int(clamp(74 * f, 0, 255)), int(clamp(128 * f, 0, 255)), int(clamp(58 * f, 0, 255)), 255))
    for i, (sx, sy) in enumerate(START):
        px = int(sx / SIZE * size)
        py = size - 1 - int(sy / SIZE * size)
        c.circle(px, py, 4, (255, 255, 255, 255), filled=True)
        c.circle(px, py, 2, ((70, 130, 210, 255) if i < 3 else (210, 90, 70, 255)), filled=True)
    c.frame(0, 0, size, size, (20, 20, 20, 255), 1)
    return c


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
             "  extentMax = X:%.2f Y:%.2f Z:0.00" % (SIZE, SIZE),
             "  nameLookupTag = MAP:StarterTeams",
             "  InitialCameraPosition = X:%.2f Y:%.2f Z:0.00" % START[0]]
    for i, (sx, sy) in enumerate(START):
        cache.append("  Player_%d_Start = X:%.2f Y:%.2f Z:0.00" % (i + 1, sx, sy))
    cache += ["END", ""]
    folder = os.path.join(out, "maps", NAME)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, NAME + ".map"), "wb") as f:
        f.write(data)
    with open(os.path.join(folder, NAME + ".tga"), "wb") as f:
        f.write(preview(m).to_tga(alpha=False, rle=True))
    with open(os.path.join(folder, "map.str"), "w", newline="") as f:
        f.write(MAP_STR)
    os.makedirs(os.path.join(out, "append", "maps"), exist_ok=True)
    with open(os.path.join(out, "append", "maps", "mapcache.ini"), "w", newline="") as f:
        f.write("\n".join(cache))


if __name__ == "__main__":
    main(sys.argv[1])
