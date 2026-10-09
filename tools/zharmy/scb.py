"""Skirmish script files (``.scb``): reader, writer and the renaming the army package format needs.

The container is the engine's DataChunk format (Core/GameEngine/Source/Common/System/DataChunk.cpp)::

    "CkMp" int32 count, then per symbol: uint8 length, name bytes, uint32 id            (table of contents)
    chunks: uint32 id, uint16 version, int32 data size, data                            (nestable, little endian)

The contents are the chunks of Scripts.cpp / SidesList.cpp::

    ScriptsPlayers  v1/v2   [int32 hasDicts (v2)] int32 count, per player: ascii name [, dict]
    PlayerScriptsList v1    ScriptList v1 per player, in the order of ScriptsPlayers
      ScriptList      v1      Script / ScriptGroup chunks
      ScriptGroup     v1/v2   ascii name, byte active, [byte subroutine (v2)], then Script chunks
      Script          v1/v2   4 ascii strings (name, comment, condition comment, action comment), 6 bytes
                              (active, oneShot, easy, normal, hard, subroutine), [int32 delay (v2)],
                              then OrCondition / ScriptAction / ScriptActionFalse chunks
      OrCondition     v1      Condition chunks
      Condition       v1..v4  int32 type, [name key (v4)], int32 count, parameters
      ScriptAction(False) v1/v2  int32 type, [name key (v2)], int32 count, parameters
      parameter:      int32 type; COORD3D: 3 float32; else int32, float32, ascii string
    ScriptTeams     v1      dictionaries (teamName, teamOwner, teamUnitType1, ...)

Everything is kept as parsed (versions, flags, numbers); only strings are changed. The symbol table is rebuilt
on write, so name keys and dictionary keys are written as strings internally.
"""

import struct

# Parameter types (Scripts.h Parameter::ParameterType)
PARAM_NAMES = [
    "INT", "REAL", "SCRIPT", "TEAM", "COUNTER", "FLAG", "COMPARISON", "WAYPOINT", "BOOLEAN", "TRIGGER_AREA",
    "TEXT_STRING", "SIDE", "SOUND", "SCRIPT_SUBROUTINE", "UNIT", "OBJECT_TYPE", "COORD3D", "ANGLE", "TEAM_STATE",
    "RELATION", "AI_MOOD", "DIALOG", "MUSIC", "MOVIE", "WAYPOINT_PATH", "LOCALIZED_TEXT", "BRIDGE",
    "KIND_OF_PARAM", "ATTACK_PRIORITY_SET", "RADAR_EVENT_TYPE", "SPECIAL_POWER", "SCIENCE", "UPGRADE",
    "COMMANDBUTTON_ABILITY", "BOUNDARY", "BUILDABLE", "SURFACES_ALLOWED", "SHAKE_INTENSITY", "COMMAND_BUTTON",
    "FONT_NAME", "OBJECT_STATUS", "COMMANDBUTTON_ALL_ABILITIES", "SKIRMISH_WAYPOINT_PATH", "COLOR", "EMOTICON",
    "OBJECT_PANEL_FLAG", "FACTION_NAME", "OBJECT_TYPE_LIST", "REVEALNAME", "SCIENCE_AVAILABILITY",
    "LEFT_OR_RIGHT", "PERCENT",
]
COORD3D = PARAM_NAMES.index("COORD3D")

DICT_BOOL, DICT_INT, DICT_REAL, DICT_ASCII, DICT_UNICODE = 0, 1, 2, 3, 4


class ScbError(ValueError):
    pass


class Dict:
    """Ordered key/value pairs like the engine's ``Dict`` (values: bool, int, float, str, or ``Unicode``)."""

    def __init__(self):
        self.pairs = []

    def get(self, key, default=None):
        for k, v in self.pairs:
            if k == key:
                return v
        return default

    def set(self, key, value):
        for i, (k, _v) in enumerate(self.pairs):
            if k == key:
                self.pairs[i] = (key, value)
                return
        self.pairs.append((key, value))

    def copy(self):
        d = Dict()
        d.pairs = list(self.pairs)
        return d


class Unicode(str):
    pass


class Param:
    __slots__ = ("type", "coord", "int", "real", "string")

    def __init__(self, ptype, coord=None, int_=0, real=0.0, string=""):
        self.type, self.coord, self.int, self.real, self.string = ptype, coord, int_, real, string

    @property
    def kind(self):
        return PARAM_NAMES[self.type] if 0 <= self.type < len(PARAM_NAMES) else "?%d" % self.type


class Call:
    """A condition or an action."""

    def __init__(self, version, ctype, name, params):
        self.version, self.type, self.name, self.params = version, ctype, name, params


class Script:
    def __init__(self):
        self.version = 2
        self.name = ""
        self.comment = ""
        self.condition_comment = ""
        self.action_comment = ""
        self.flags = [1, 0, 1, 1, 1, 0]       # active, oneShot, easy, normal, hard, subroutine
        self.delay = 0
        self.or_conditions = []                # list of (version, [Call])
        self.actions = []
        self.false_actions = []


class Group:
    def __init__(self):
        self.version = 2
        self.name = ""
        self.active = 1
        self.subroutine = 0
        self.scripts = []


class ScriptList:
    def __init__(self):
        self.version = 1
        self.items = []                        # Script / Group

    def all_scripts(self):
        for it in self.items:
            if isinstance(it, Group):
                yield from it.scripts
            else:
                yield it


class Scb:
    def __init__(self):
        self.players_version = 2
        self.has_dicts = 1
        self.players = []                      # [(name, Dict or None)]
        self.lists = []                        # ScriptList, parallel to players (may be shorter)
        self.lists_version = 1
        self.teams_version = 1
        self.teams = []                        # Dict
        self.other = []                        # unknown top level chunks: (name, version, bytes)


# ---- reading ----------------------------------------------------------------------------------------------------
class _Reader:
    def __init__(self, data):
        if data[:4] != b"CkMp":
            raise ScbError("missing table of contents (not a data chunk file)")
        (count,) = struct.unpack_from("<i", data, 4)
        pos = 8
        self.names = {}
        for _ in range(count):
            n = data[pos]
            name = data[pos + 1:pos + 1 + n].decode("latin-1")
            (ident,) = struct.unpack_from("<I", data, pos + 1 + n)
            self.names[ident] = name
            pos += n + 5
        self.data = data
        self.start = pos

    def chunks(self, pos, end):
        while pos < end:
            if pos + 10 > end:
                raise ScbError("truncated chunk header")
            ident, version, size = struct.unpack_from("<IHi", self.data, pos)
            if ident not in self.names:
                raise ScbError("unknown chunk id %d" % ident)
            if size < 0 or pos + 10 + size > end:
                raise ScbError("chunk %s overruns its parent" % self.names[ident])
            yield self.names[ident], version, pos + 10, size
            pos += 10 + size


class _Cur:
    def __init__(self, reader, pos):
        self.r, self.pos = reader, pos

    def int(self):
        (v,) = struct.unpack_from("<i", self.r.data, self.pos)
        self.pos += 4
        return v

    def real(self):
        (v,) = struct.unpack_from("<f", self.r.data, self.pos)
        self.pos += 4
        return v

    def byte(self):
        v = self.r.data[self.pos]
        self.pos += 1
        return v

    def ascii(self):
        (n,) = struct.unpack_from("<H", self.r.data, self.pos)
        v = self.r.data[self.pos + 2:self.pos + 2 + n].decode("latin-1")
        self.pos += 2 + n
        return v

    def unicode(self):
        (n,) = struct.unpack_from("<H", self.r.data, self.pos)
        v = self.r.data[self.pos + 2:self.pos + 2 + n * 2].decode("utf-16-le")
        self.pos += 2 + n * 2
        return Unicode(v)

    def namekey(self):
        key = self.int()
        ident = key >> 8
        if ident not in self.r.names:
            raise ScbError("bad name key")
        return self.r.names[ident]

    def dict(self):
        (count,) = struct.unpack_from("<H", self.r.data, self.pos)
        self.pos += 2
        d = Dict()
        for _ in range(count):
            raw = self.int()
            kind = raw & 0xFF
            key = self.r.names[raw >> 8]
            if kind == DICT_BOOL:
                value = bool(self.byte())
            elif kind == DICT_INT:
                value = self.int()
            elif kind == DICT_REAL:
                value = self.real()
            elif kind == DICT_ASCII:
                value = self.ascii()
            elif kind == DICT_UNICODE:
                value = self.unicode()
            else:
                raise ScbError("bad dictionary value type %d" % kind)
            d.pairs.append((key, value))
        return d

    def param(self):
        ptype = self.int()
        if ptype == COORD3D:
            return Param(ptype, coord=(self.real(), self.real(), self.real()))
        return Param(ptype, None, self.int(), self.real(), self.ascii())


def _read_call(r, off, version, has_name):
    cur = _Cur(r, off)
    ctype = cur.int()
    name = cur.namekey() if has_name else None
    count = cur.int()
    return Call(version, ctype, name, [cur.param() for _ in range(count)])


def _read_script(r, version, off, size):
    cur = _Cur(r, off)
    s = Script()
    s.version = version
    s.name, s.comment, s.condition_comment, s.action_comment = cur.ascii(), cur.ascii(), cur.ascii(), cur.ascii()
    s.flags = [cur.byte() for _ in range(6)]
    s.delay = cur.int() if version >= 2 else 0
    for name, v, o, sz in r.chunks(cur.pos, off + size):
        if name == "OrCondition":
            conds = [_read_call(r, co, cv, cv >= 4) for cn, cv, co, cs in r.chunks(o, o + sz) if cn == "Condition"]
            s.or_conditions.append((v, conds))
        elif name == "ScriptAction":
            s.actions.append(_read_call(r, o, v, v >= 2))
        elif name == "ScriptActionFalse":
            s.false_actions.append(_read_call(r, o, v, v >= 2))
        else:
            raise ScbError("unexpected chunk %s in a script" % name)
    return s


def read_scb(data):
    r = _Reader(data)
    scb = Scb()
    for name, version, off, size in r.chunks(r.start, len(data)):
        if name == "ScriptsPlayers":
            cur = _Cur(r, off)
            scb.players_version = version
            scb.has_dicts = cur.int() if version >= 2 else 0
            for _ in range(cur.int()):
                pname = cur.ascii()
                scb.players.append((pname, cur.dict() if scb.has_dicts else None))
        elif name == "PlayerScriptsList":
            scb.lists_version = version
            for ln, lv, lo, ls in r.chunks(off, off + size):
                if ln != "ScriptList":
                    raise ScbError("unexpected chunk %s in PlayerScriptsList" % ln)
                sl = ScriptList()
                sl.version = lv
                for sn, sv, so, ss in r.chunks(lo, lo + ls):
                    if sn == "Script":
                        sl.items.append(_read_script(r, sv, so, ss))
                    elif sn == "ScriptGroup":
                        cur = _Cur(r, so)
                        g = Group()
                        g.version = sv
                        g.name, g.active = cur.ascii(), cur.byte()
                        g.subroutine = cur.byte() if sv >= 2 else 0
                        for gn, gv, go, gs in r.chunks(cur.pos, so + ss):
                            if gn != "Script":
                                raise ScbError("unexpected chunk %s in a script group" % gn)
                            g.scripts.append(_read_script(r, gv, go, gs))
                        sl.items.append(g)
                    else:
                        raise ScbError("unexpected chunk %s in a script list" % sn)
                scb.lists.append(sl)
        elif name == "ScriptTeams":
            scb.teams_version = version
            cur = _Cur(r, off)
            while cur.pos < off + size:
                scb.teams.append(cur.dict())
        else:
            scb.other.append((name, version, data[off:off + size]))
    return scb


# ---- writing ----------------------------------------------------------------------------------------------------
class _Writer:
    def __init__(self):
        self.ids = {}
        self.order = []
        self.body = bytearray()
        self.stack = []

    def sym(self, name):
        if name not in self.ids:
            self.ids[name] = len(self.order) + 1
            self.order.append(name)
        return self.ids[name]

    def open(self, name, version):
        self.body += struct.pack("<IH", self.sym(name), version)
        self.stack.append(len(self.body))
        self.body += struct.pack("<i", 0)

    def close(self):
        pos = self.stack.pop()
        struct.pack_into("<i", self.body, pos, len(self.body) - pos - 4)

    def int(self, v):
        self.body += struct.pack("<i", v)

    def real(self, v):
        self.body += struct.pack("<f", v)

    def byte(self, v):
        self.body += struct.pack("<B", v & 0xFF)

    def ascii(self, s):
        raw = s.encode("latin-1")
        self.body += struct.pack("<H", len(raw)) + raw

    def unicode(self, s):
        raw = s.encode("utf-16-le")
        self.body += struct.pack("<H", len(raw) // 2) + raw

    def namekey(self, name):
        self.body += struct.pack("<i", (self.sym(name) << 8) | DICT_ASCII)

    def dict(self, d):
        self.body += struct.pack("<H", len(d.pairs))
        for key, value in d.pairs:
            if isinstance(value, bool):
                kind = DICT_BOOL
            elif isinstance(value, int):
                kind = DICT_INT
            elif isinstance(value, float):
                kind = DICT_REAL
            elif isinstance(value, Unicode):
                kind = DICT_UNICODE
            else:
                kind = DICT_ASCII
            self.body += struct.pack("<i", (self.sym(key) << 8) | kind)
            if kind == DICT_BOOL:
                self.byte(1 if value else 0)
            elif kind == DICT_INT:
                self.int(value)
            elif kind == DICT_REAL:
                self.real(value)
            elif kind == DICT_ASCII:
                self.ascii(value)
            else:
                self.unicode(value)

    def param(self, p):
        self.int(p.type)
        if p.type == COORD3D:
            for v in p.coord:
                self.real(v)
        else:
            self.int(p.int)
            self.real(p.real)
            self.ascii(p.string)

    def bytes(self):
        toc = bytearray(b"CkMp") + struct.pack("<i", len(self.order))
        for name in self.order:
            raw = name.encode("latin-1")
            toc += struct.pack("<B", len(raw)) + raw + struct.pack("<I", self.ids[name])
        return bytes(toc) + bytes(self.body)


def _write_call(w, chunk, call):
    w.open(chunk, call.version)
    w.int(call.type)
    if call.name is not None:
        w.namekey(call.name)
    w.int(len(call.params))
    for p in call.params:
        w.param(p)
    w.close()


def _write_script(w, s):
    w.open("Script", s.version)
    for t in (s.name, s.comment, s.condition_comment, s.action_comment):
        w.ascii(t)
    for f in s.flags:
        w.byte(f)
    if s.version >= 2:
        w.int(s.delay)
    for version, conds in s.or_conditions:
        w.open("OrCondition", version)
        for c in conds:
            _write_call(w, "Condition", c)
        w.close()
    for a in s.actions:
        _write_call(w, "ScriptAction", a)
    for a in s.false_actions:
        _write_call(w, "ScriptActionFalse", a)
    w.close()


def write_scb(scb):
    w = _Writer()
    w.open("ScriptsPlayers", scb.players_version)
    if scb.players_version >= 2:
        w.int(scb.has_dicts)
    w.int(len(scb.players))
    for name, d in scb.players:
        w.ascii(name)
        if scb.players_version >= 2 and scb.has_dicts:
            w.dict(d or Dict())
    w.close()
    w.open("PlayerScriptsList", scb.lists_version)
    for sl in scb.lists:
        w.open("ScriptList", sl.version)
        for it in sl.items:
            if isinstance(it, Group):
                w.open("ScriptGroup", it.version)
                w.ascii(it.name)
                w.byte(it.active)
                if it.version >= 2:
                    w.byte(it.subroutine)
                for s in it.scripts:
                    _write_script(w, s)
                w.close()
            else:
                _write_script(w, it)
        w.close()
    w.close()
    w.open("ScriptTeams", scb.teams_version)
    for t in scb.teams:
        w.dict(t)
    w.close()
    for name, version, raw in scb.other:
        w.open(name, version)
        w.body += raw
        w.close()
    return w.bytes()


# ---- renaming ---------------------------------------------------------------------------------------------------
# parameter types that name something the package can rename, with the map to use
SCRIPT_NAME_TYPES = ("SCRIPT", "SCRIPT_SUBROUTINE")
LOCAL_NAME_TYPES = ("TEAM", "COUNTER", "FLAG")        # prefixed with the tag unless they are special tokens
DEFINITION_TYPES = {"OBJECT_TYPE": "Object", "UPGRADE": "Upgrade", "SCIENCE": "Science",
                    "SPECIAL_POWER": "SpecialPower", "COMMAND_BUTTON": "CommandButton",
                    "SOUND": "Audio", "DIALOG": "Audio", "MUSIC": "Audio"}
TEAM_SCRIPT_KEYS = ("teamOnCreateScript", "teamOnIdleScript", "teamOnUnitDestroyedScript", "teamOnDestroyedScript",
                    "teamEnemySightedScript", "teamAllClearScript", "teamProductionCondition")


class Renamer:
    """Rewrites the strings of one script list and its teams.

    ``prefix``: e.g. ``"CNUK_"``; ``definitions``: {kind: {lower old name: new name}};
    ``sides``: {lower old player/side name: new}; ``labels``: {lower old label: new label}.
    ``used``: filled with the (kind, old name) pairs found, for the closure walk and the report.
    """

    def __init__(self, prefix, definitions, sides=None, labels=None):
        self.prefix = prefix
        self.definitions = definitions
        self.sides = sides or {}
        self.labels = labels or {}
        self.used = []
        self.script_names = {}

    def local(self, name):
        if not name or name.startswith("<") or name.lower().startswith(self.prefix.lower()):
            return name
        return self.prefix + name

    def prepare(self, slist):
        for s in slist.all_scripts():
            self.script_names[s.name.lower()] = self.local(s.name)

    def script_ref(self, name):
        if not name or name.startswith("<"):
            return name
        return self.script_names.get(name.lower(), name)

    def call(self, call):
        for p in call.params:
            kind = p.kind
            if p.type == COORD3D:
                continue
            if kind in SCRIPT_NAME_TYPES:
                p.string = self.script_ref(p.string)
            elif kind in LOCAL_NAME_TYPES:
                p.string = self.local(p.string)
            elif kind in DEFINITION_TYPES:
                k = DEFINITION_TYPES[kind]
                if p.string:
                    self.used.append((k, p.string))
                    new = self.definitions.get(k, {}).get(p.string.lower())
                    if new:
                        p.string = new
            elif kind == "SIDE":
                new = self.sides.get(p.string.lower())
                if new:
                    p.string = new
            elif kind in ("TEXT_STRING", "LOCALIZED_TEXT"):
                new = self.labels.get(p.string.lower())
                if new:
                    p.string = new

    def script(self, s):
        s.name = self.script_names.get(s.name.lower(), s.name)
        for _v, conds in s.or_conditions:
            for c in conds:
                self.call(c)
        for a in s.actions + s.false_actions:
            self.call(a)

    def script_list(self, slist):
        self.prepare(slist)
        for it in slist.items:
            if isinstance(it, Group):
                for s in it.scripts:
                    self.script(s)
            else:
                self.script(it)

    def team(self, d, owner_old, owner_new):
        out = d.copy()
        out.pairs = []
        for k, v in d.pairs:
            if k == "teamName" and isinstance(v, str):
                v = self.team_name(v, owner_old, owner_new)
            elif k == "teamOwner" and isinstance(v, str):
                v = owner_new
            elif (k.startswith("teamUnitType") or k == "teamTransport") and isinstance(v, str) and v:
                self.used.append(("Object", v))
                v = self.definitions.get("Object", {}).get(v.lower(), v)
            elif (k in TEAM_SCRIPT_KEYS or k.startswith("teamGenericScriptHook")) and isinstance(v, str) and v:
                v = self.script_ref(v)
            out.pairs.append((k, v))
        return out

    def team_name(self, name, owner_old, owner_new):
        # a player's default team is called "team<player name>"
        if name.lower() == ("team" + owner_old).lower():
            return "team" + owner_new
        return self.local(name)


def extract_for_player(scb, player_name):
    """Return (ScriptList, [team Dict]) of one player of a parsed skirmish file, or (None, [])."""
    for i, (name, _d) in enumerate(scb.players):
        if name.lower() == player_name.lower() and i < len(scb.lists):
            teams = [t for t in scb.teams if str(t.get("teamOwner", "")).lower() == name.lower()]
            return scb.lists[i], teams, name
    return None, [], None
