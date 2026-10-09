"""Script (``.scb`` and map ``PlayerScriptsList``) writer and reader.

Mirrors the engine's script data chunks (GameLogic/ScriptEngine/Scripts.cpp)::

    PlayerScriptsList v1
      ScriptList v1                     one per player, in player order
        Script v2 ...                   scripts outside groups
        ScriptGroup v2 ...              name, active, subroutine, then Script chunks
      Script v2:  name, comment, conditionComment, actionComment, bytes (active, oneShot, easy, normal, hard,
                  subroutine), int delaySeconds, then OrCondition*, ScriptAction*, ScriptActionFalse*
        OrCondition v1:  Condition v4*: int type, name key, int numParms, parms
        ScriptAction v2: int type, name key, int numParms, parms
        parm: int parameterType; COORD3D: 3 reals; otherwise int, real, ascii string

The numeric ids are positions in the enums of ``GeneralsMD/Code/GameEngine/Include/GameLogic/Scripts.h`` and the
parameter lists are the templates registered in ``ScriptEngine.cpp``. Only the subset the starter pack uses is
listed here; ``tools/tests/test_scripts.py`` checks every entry against the engine sources.
"""

from .datachunk import ChunkWriter, Dict

# ParameterType (Scripts.h): name -> id
PARAM = {
    "INT": 0, "REAL": 1, "SCRIPT": 2, "TEAM": 3, "COUNTER": 4, "FLAG": 5, "COMPARISON": 6, "WAYPOINT": 7, "BOOLEAN": 8,
    "TRIGGER_AREA": 9, "TEXT_STRING": 10, "SIDE": 11, "SOUND": 12, "SCRIPT_SUBROUTINE": 13, "UNIT": 14,
    "OBJECT_TYPE": 15, "COORD3D": 16, "ANGLE": 17, "TEAM_STATE": 18, "RELATION": 19, "AI_MOOD": 20, "DIALOG": 21,
    "MUSIC": 22, "MOVIE": 23, "WAYPOINT_PATH": 24,
}
INT_PARAMS = {"INT", "COMPARISON", "BOOLEAN", "RELATION", "AI_MOOD"}
REAL_PARAMS = {"REAL", "ANGLE"}

# Comparison values (Scripts.h Parameter::ComparisonType)
LESS_THAN, LESS_EQUAL, EQUAL, GREATER_EQUAL, GREATER, NOT_EQUAL = range(6)

# AI moods (AIAttitude): sleep, passive, normal, alert, aggressive, invalid
ATTITUDE_SLEEP, ATTITUDE_PASSIVE, ATTITUDE_NORMAL, ATTITUDE_ALERT, ATTITUDE_AGGRESSIVE = -2, -1, 0, 1, 2

THIS_TEAM = "<This Team>"
THIS_PLAYER = "<This Player>"

# name -> (id, [parameter type names])
ACTIONS = {
    "VICTORY": (3, []),
    "DEFEAT": (4, []),
    "LOCALDEFEAT": (295, []),
    "NO_OP": (5, []),
    "TEAM_SET_ATTITUDE": (46, ["TEAM", "AI_MOOD"]),
    "TEAM_GUARD": (58, ["TEAM"]),
    "TEAM_HUNT": (60, ["TEAM"]),
    "PLAYER_HUNT": (96, ["SIDE"]),
    "SKIRMISH_BUILD_BUILDING": (244, ["OBJECT_TYPE"]),
    "MUSIC_SET_TRACK": (99, ["MUSIC", "BOOLEAN", "BOOLEAN"]),
}
CONDITIONS = {
    "CONDITION_FALSE": (0, []),
    "CONDITION_TRUE": (3, []),
    "MULTIPLAYER_ALLIED_VICTORY": (43, []),
    "MULTIPLAYER_ALLIED_DEFEAT": (44, []),
    "MULTIPLAYER_PLAYER_DEFEAT": (45, []),
    "PLAYER_HAS_CREDITS": (26, ["INT", "COMPARISON", "SIDE"]),
}


class Parameter:
    def __init__(self, kind, value):
        self.kind = kind
        self.value = value

    def write(self, w):
        w.int(PARAM[self.kind])
        if self.kind == "COORD3D":
            for v in self.value:
                w.real(float(v))
            return
        i, r, s = 0, 0.0, ""
        if self.kind in INT_PARAMS:
            i = int(self.value)
        elif self.kind in REAL_PARAMS:
            r = float(self.value)
        else:
            s = str(self.value)
        w.int(i)
        w.real(r)
        w.ascii(s)


class Call:
    """A condition or an action: a template name plus its parameter values."""

    def __init__(self, table, name, *values):
        if name not in table:
            raise KeyError("unknown script template %s" % name)
        ident, kinds = table[name]
        if len(values) != len(kinds):
            raise ValueError("%s takes %d parameters, got %d" % (name, len(kinds), len(values)))
        self.name = name
        self.ident = ident
        self.params = [Parameter(k, v) for k, v in zip(kinds, values)]

    def write(self, w, chunk, version):
        with w.chunk(chunk, version):
            w.int(self.ident)
            w.name_key(self.name)
            w.int(len(self.params))
            for p in self.params:
                p.write(w)


def condition(name, *values):
    return Call(CONDITIONS, name, *values)


def action(name, *values):
    return Call(ACTIONS, name, *values)


class Script:
    def __init__(self, name, conditions=None, actions=None, false_actions=None, subroutine=False, one_shot=False,
                 active=True, easy=True, normal=True, hard=True, delay_seconds=0, comment=""):
        self.name = name
        # outer list: OR terms; inner list: AND conditions
        self.conditions = conditions if conditions is not None else [[condition("CONDITION_TRUE")]]
        self.actions = actions or []
        self.false_actions = false_actions or []
        self.subroutine = subroutine
        self.one_shot = one_shot
        self.active = active
        self.difficulty = (easy, normal, hard)
        self.delay_seconds = delay_seconds
        self.comment = comment

    def write(self, w):
        with w.chunk("Script", 2):
            w.ascii(self.name)
            w.ascii(self.comment)
            w.ascii("")
            w.ascii("")
            for flag in (self.active, self.one_shot) + self.difficulty + (self.subroutine,):
                w.byte(1 if flag else 0)
            w.int(self.delay_seconds)
            for ands in self.conditions:
                with w.chunk("OrCondition", 1):
                    for c in ands:
                        c.write(w, "Condition", 4)
            for a in self.actions:
                a.write(w, "ScriptAction", 2)
            for a in self.false_actions:
                a.write(w, "ScriptActionFalse", 2)


class ScriptList:
    def __init__(self, scripts=()):
        self.scripts = list(scripts)

    def write(self, w):
        with w.chunk("ScriptList", 1):
            for s in self.scripts:
                s.write(w)


def write_player_scripts(w, lists):
    """Write the ``PlayerScriptsList`` chunk for the given script lists (one per player, in order)."""
    with w.chunk("PlayerScriptsList", 1):
        for sl in lists:
            sl.write(w)


def write_skirmish_file(players, teams):
    """``SkirmishScripts.scb``: ScriptsPlayers (names, in the order of the script lists), PlayerScriptsList and
    ScriptTeams (a sequence of team dictionaries)."""
    w = ChunkWriter()
    with w.chunk("ScriptsPlayers", 2):
        w.int(1)                    # ZH (version 2): a dictionary follows every name
        w.int(len(players))
        for name, _scripts in players:
            w.ascii(name)
            w.dict(Dict())
    write_player_scripts(w, [ScriptList(s) for _n, s in players])
    with w.chunk("ScriptTeams", 1):
        for t in teams:
            w.dict(t)
    return w.to_bytes()


# ------------------------------------------------------------------------------------------------- reader

def read_player_scripts(reader, offset, size):
    """Parse a ``PlayerScriptsList`` chunk body. Returns a list (per player) of lists of dicts describing scripts."""
    out = []
    for name, _v, off, sz in reader.chunks(offset, offset + size):
        if name != "ScriptList":
            raise ValueError("unexpected chunk %s in PlayerScriptsList" % name)
        scripts = []
        for sname, sver, soff, ssz in reader.chunks(off, off + sz):
            if sname == "Script":
                scripts.append(_read_script(reader, sver, soff, ssz))
            elif sname == "ScriptGroup":
                cur = reader.cursor(soff)
                gname, active, sub = cur.ascii(), cur.byte(), cur.byte()
                members = [_read_script(reader, v2, o2, s2)
                           for n2, v2, o2, s2 in reader.chunks(cur.pos, soff + ssz) if n2 == "Script"]
                scripts.append({"group": gname, "active": bool(active), "subroutine": bool(sub), "scripts": members})
            else:
                raise ValueError("unexpected chunk %s in ScriptList" % sname)
        out.append(scripts)
    return out


def _read_script(reader, version, offset, size):
    cur = reader.cursor(offset)
    s = {"name": cur.ascii(), "comment": cur.ascii(), "condition_comment": cur.ascii(), "action_comment": cur.ascii()}
    s["active"], s["one_shot"], s["easy"], s["normal"], s["hard"], s["subroutine"] = (bool(cur.byte()) for _ in range(6))
    s["delay"] = cur.int() if version >= 2 else 0
    s["conditions"], s["actions"], s["false_actions"] = [], [], []
    for name, v, off, sz in reader.chunks(cur.pos, offset + size):
        if name == "OrCondition":
            ands = []
            for cname, cv, coff, csz in reader.chunks(off, off + sz):
                ands.append(_read_call(reader, coff, cv >= 4))
            s["conditions"].append(ands)
        elif name == "ScriptAction":
            s["actions"].append(_read_call(reader, off, v >= 2))
        elif name == "ScriptActionFalse":
            s["false_actions"].append(_read_call(reader, off, v >= 2))
        else:
            raise ValueError("unexpected chunk %s in Script" % name)
    return s


def _read_call(reader, offset, has_name):
    cur = reader.cursor(offset)
    call = {"id": cur.int()}
    if has_name:
        key = cur.int()
        call["name"] = reader.names[key >> 8]
    n = cur.int()
    params = []
    names = {v: k for k, v in PARAM.items()}
    for _ in range(n):
        kind = names[cur.int()]
        if kind == "COORD3D":
            params.append((kind, (cur.real(), cur.real(), cur.real())))
        else:
            i, r, s = cur.int(), cur.real(), cur.ascii()
            params.append((kind, i if kind in INT_PARAMS else r if kind in REAL_PARAMS else s))
    call["params"] = params
    return call
