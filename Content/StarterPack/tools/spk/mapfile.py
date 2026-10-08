"""Map (``.map``) writer and reader.

A map is a DataChunk stream (see :mod:`spk.datachunk`) with these chunks, in the order the editor writes them and
the engine needs them (WorldInfo before SidesList before ObjectsList)::

    HeightMapData v4   int width, height, borderSize, numBoundaries, (x, y)*, dataSize, bytes (row major, one per vertex)
    BlendTileData v8   int dataSize; int16 tile[], blend[], extraBlend[], cliffInfo[] (dataSize each); cliff state bits
                       (height rows of (width+7)/8 bytes); int numBitmapTiles, numBlendedTiles, numCliffInfo;
                       int numTextureClasses then per class: firstTile, numTiles, width, 0, name;
                       int numEdgeTiles, numEdgeTextureClasses (+ classes); blended tiles and cliff infos from index 1
    WorldInfo v1       Dict (mapName, weather)
    SidesList v3       int numSides; per side: Dict, int numBuildList, build list entries; int numTeams; team Dicts;
                       a nested PlayerScriptsList chunk
    ObjectsList v3     Object v3 chunks: real x, y, z, angle; int flags; ascii name; Dict
    PolygonTriggers v4 int count; ...
    GlobalLighting v3  int timeOfDay; per time of day (4): terrain light 0, object light 0, object lights 1-2,
                       terrain lights 1-2 (each: ambient rgb, diffuse rgb, direction xyz); uint shadow colour
    WaypointsList v1   int numLinks; (int, int)*

Tile index of a cell: ``((firstTile + (x/2)%w + w*((y/2)%w)) << 2) + 2*(y&1) + (x&1)`` for a texture class of
``w`` x ``w`` tiles of 64 pixels (WorldHeightMapEdit::getTileNdxForClass); a terrain cell is 10 world units, and the
playable area is the map minus ``border`` cells on every side.
"""

import struct

from .datachunk import ChunkWriter, ChunkReader, Dict, Unicode

FLAG_VAL = 0x7ADA0000
MAP_XY_FACTOR = 10.0
MAP_HEIGHT_SCALE = MAP_XY_FACTOR / 16.0

TIME_MORNING, TIME_AFTERNOON, TIME_EVENING, TIME_NIGHT = 1, 2, 3, 4


class Light:
    def __init__(self, ambient=(0.0, 0.0, 0.0), diffuse=(0.0, 0.0, 0.0), direction=(0.0, 0.0, -1.0)):
        self.ambient, self.diffuse, self.direction = ambient, diffuse, direction

    def write(self, w):
        for v in self.ambient + self.diffuse + self.direction:
            w.real(float(v))


class TextureClass:
    def __init__(self, name, first_tile, num_tiles=4, width=2):
        self.name, self.first_tile, self.num_tiles, self.width = name, first_tile, num_tiles, width


class MapObject:
    def __init__(self, name, x, y, z=0.0, angle=0.0, flags=0, props=None):
        self.name, self.x, self.y, self.z, self.angle, self.flags = name, x, y, z, angle, flags
        self.props = props if props is not None else Dict()


class BuildEntry:
    def __init__(self, name, template, x, y, angle=0.0, built=True, rebuilds=0):
        self.name, self.template, self.x, self.y, self.angle = name, template, x, y, angle
        self.built, self.rebuilds = built, rebuilds


class Map:
    def __init__(self, width, height, border):
        self.width, self.height, self.border = width, height, border
        self.heights = bytearray(width * height)
        self.tiles = [0] * (width * height)
        self.classes = []
        self.world = Dict()
        self.sides = []         # (Dict, [BuildEntry])
        self.teams = []         # [Dict]
        self.scripts = None     # list of ScriptList (one per side) or None
        self.objects = []
        self.lighting = {}      # time of day -> dict(terrain=[3 Light], objects=[3 Light])
        self.time_of_day = TIME_AFTERNOON
        self.shadow_color = 0x7FA0A0A0
        self.links = []
        self.triggers = []

    @property
    def num_tiles(self):
        return sum(c.num_tiles for c in self.classes)

    def tile_index(self, cls, x, y):
        c = self.classes[cls]
        base = c.first_tile + (x // 2) % c.width + c.width * ((y // 2) % c.width)
        return (base << 2) + 2 * (y & 1) + (x & 1)


def write_map(m):
    from .scripts import write_player_scripts
    w = ChunkWriter()
    n = m.width * m.height

    with w.chunk("HeightMapData", 4):
        w.int(m.width)
        w.int(m.height)
        w.int(m.border)
        w.int(1)
        w.int(m.width - 2 * m.border)
        w.int(m.height - 2 * m.border)
        w.int(n)
        w.bytes(bytes(m.heights))

    with w.chunk("BlendTileData", 8):
        w.int(n)
        w.bytes(struct.pack("<%dh" % n, *m.tiles))
        w.bytes(bytes(2 * n))               # blend tiles: none
        w.bytes(bytes(2 * n))               # extra blend tiles: none
        w.bytes(bytes(2 * n))               # cliff info: none
        w.bytes(bytes(((m.width + 7) // 8) * m.height))
        w.int(m.num_tiles)
        w.int(1)                            # blended tiles (index 0 is the opaque default)
        w.int(1)                            # cliff infos
        w.int(len(m.classes))
        for c in m.classes:
            w.int(c.first_tile)
            w.int(c.num_tiles)
            w.int(c.width)
            w.int(0)
            w.ascii(c.name)
        w.int(0)                            # edge tiles
        w.int(0)                            # edge texture classes

    with w.chunk("WorldInfo", 1):
        w.dict(m.world)

    with w.chunk("SidesList", 3):
        w.int(len(m.sides))
        for d, build in m.sides:
            w.dict(d)
            w.int(len(build))
            for b in build:
                w.ascii(b.name)
                w.ascii(b.template)
                w.real(b.x)
                w.real(b.y)
                w.real(0.0)
                w.real(b.angle)
                w.byte(1 if b.built else 0)
                w.int(b.rebuilds)
                w.ascii("")
                w.int(100)
                w.byte(0)
                w.byte(0)
                w.byte(0)
        w.int(len(m.teams))
        for t in m.teams:
            w.dict(t)
        if m.scripts is not None:
            write_player_scripts(w, m.scripts)
        else:
            from .scripts import ScriptList
            write_player_scripts(w, [ScriptList() for _ in m.sides])

    with w.chunk("ObjectsList", 3):
        for o in m.objects:
            with w.chunk("Object", 3):
                w.real(o.x)
                w.real(o.y)
                w.real(o.z)
                w.real(o.angle)
                w.int(o.flags)
                w.ascii(o.name)
                w.dict(o.props)

    with w.chunk("PolygonTriggers", 4):
        w.int(len(m.triggers))
        for t in m.triggers:
            w.ascii(t["name"])
            w.ascii(t.get("layer", ""))
            w.int(t["id"])
            w.byte(0)
            w.byte(0)
            w.int(0)
            w.int(len(t["points"]))
            for x, y, z in t["points"]:
                w.int(int(x))
                w.int(int(y))
                w.int(int(z))

    with w.chunk("GlobalLighting", 3):
        w.int(m.time_of_day)
        for tod in (TIME_MORNING, TIME_AFTERNOON, TIME_EVENING, TIME_NIGHT):
            lit = m.lighting[tod]
            terrain, objects = lit["terrain"], lit["objects"]
            terrain[0].write(w)
            objects[0].write(w)
            objects[1].write(w)
            objects[2].write(w)
            terrain[1].write(w)
            terrain[2].write(w)
        w.uint(m.shadow_color)

    with w.chunk("WaypointsList", 1):
        w.int(len(m.links))
        for a, b in m.links:
            w.int(a)
            w.int(b)
    return w.to_bytes()


# ------------------------------------------------------------------------------------------------- reader

def read_map(data):
    """Parse a map written by :func:`write_map` (or by the editor, for the chunks this module knows).

    Returns a dict with ``width``, ``height``, ``border``, ``heights``, ``tiles``, ``classes``, ``world``, ``sides``,
    ``teams``, ``scripts`` (parsed with :mod:`spk.scripts`), ``objects``, ``links`` and the chunk order."""
    from .scripts import read_player_scripts
    r = ChunkReader(data)
    out = {"order": []}
    for name, version, off, size in r.chunks():
        out["order"].append(name)
        cur = r.cursor(off)
        if name == "HeightMapData":
            out["width"], out["height"], out["border"] = cur.int(), cur.int(), cur.int()
            nb = cur.int()
            out["boundaries"] = [(cur.int(), cur.int()) for _ in range(nb)]
            n = cur.int()
            if n != out["width"] * out["height"]:
                raise ValueError("height map size mismatch")
            out["heights"] = cur.bytes(n)
            if cur.pos != off + size:
                raise ValueError("HeightMapData has trailing bytes")
        elif name == "BlendTileData":
            n = cur.int()
            if n != out["width"] * out["height"]:
                raise ValueError("blend tile size mismatch")
            out["tiles"] = list(struct.unpack("<%dh" % n, cur.bytes(2 * n)))
            for _ in range(3):
                cur.bytes(2 * n)
            cur.bytes(((out["width"] + 7) // 8) * out["height"])
            out["num_bitmap_tiles"], out["num_blended"], out["num_cliff"] = cur.int(), cur.int(), cur.int()
            classes = []
            for _ in range(cur.int()):
                first, num, width, _legacy = cur.int(), cur.int(), cur.int(), cur.int()
                classes.append(TextureClass(cur.ascii(), first, num, width))
            out["classes"] = classes
            num_edge_tiles, num_edge_classes = cur.int(), cur.int()
            for _ in range(num_edge_classes):
                cur.int(), cur.int(), cur.int(), cur.ascii()
            for _ in range(1, out["num_blended"]):
                cur.bytes(4 + 6 + 4 + 4)
            for _ in range(1, out["num_cliff"]):
                cur.bytes(4 + 32 + 2)
            if cur.pos != off + size:
                raise ValueError("BlendTileData has trailing bytes (%d)" % (off + size - cur.pos))
        elif name == "WorldInfo":
            out["world"] = cur.dict()
        elif name == "SidesList":
            sides = []
            for _ in range(cur.int()):
                d = cur.dict()
                build = []
                for _b in range(cur.int()):
                    b = BuildEntry(cur.ascii(), cur.ascii(), cur.real(), cur.real(), 0.0)
                    cur.real()
                    b.angle = cur.real()
                    b.built = bool(cur.byte())
                    b.rebuilds = cur.int()
                    cur.ascii(), cur.int(), cur.byte(), cur.byte(), cur.byte()
                    build.append(b)
                sides.append((d, build))
            out["sides"] = sides
            out["teams"] = [cur.dict() for _ in range(cur.int())]
            for cname, _v, coff, csize in r.chunks(cur.pos, off + size):
                if cname == "PlayerScriptsList":
                    out["scripts"] = read_player_scripts(r, coff, csize)
        elif name == "ObjectsList":
            objs = []
            for oname, _v, ooff, osize in r.chunks(off, off + size):
                oc = r.cursor(ooff)
                o = MapObject("", oc.real(), oc.real(), oc.real(), oc.real(), oc.int())
                o.name = oc.ascii()
                o.props = oc.dict()
                if oc.pos != ooff + osize:
                    raise ValueError("Object has trailing bytes")
                objs.append(o)
            out["objects"] = objs
        elif name == "PolygonTriggers":
            out["triggers"] = cur.int()
        elif name == "GlobalLighting":
            out["time_of_day"] = cur.int()
            per_tod = (9 * 6)
            cur.bytes(4 * per_tod * 4)
            out["shadow_color"] = cur.int() & 0xFFFFFFFF
            if cur.pos != off + size:
                raise ValueError("GlobalLighting has trailing bytes")
        elif name == "WaypointsList":
            out["links"] = [(cur.int(), cur.int()) for _ in range(cur.int())]
    return out
