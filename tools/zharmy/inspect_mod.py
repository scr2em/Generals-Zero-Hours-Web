"""``zharmy inspect``: list the playable factions of a mod."""

from . import scb as scbmod, strings as stringsmod
from .convert import build_vfs
from .gamedata import GameData


def inspect_factions(mod_paths, base_paths=None, language=None, mod_archives=None, progress=None):
    progress = progress or (lambda m: None)
    progress("reading archive directories")
    mod_vfs, base_vfs = build_vfs(mod_paths, base_paths, mod_archives)
    progress("loading definitions (%d archives/folders)" % len(mod_vfs.layers))
    data = GameData(mod_vfs, "mod")
    strings = stringsmod.load_strings(mod_vfs, language)
    scb = None
    if mod_vfs.exists("data/scripts/skirmishscripts.scb"):
        try:
            scb = scbmod.read_scb(mod_vfs.read("data/scripts/skirmishscripts.scb"))
        except (scbmod.ScbError, ValueError, IndexError):
            scb = None
    players = {n.lower() for n, _d in scb.players} if scb else set()
    base_names = set()
    if base_vfs is not None:
        progress("loading ruleset definitions")
        base_names = {d.key for d in GameData(base_vfs, "base").defs.get("PlayerTemplate", {}).values()}
    out = []
    for d in data.ordered("PlayerTemplate"):
        node = d.node
        if (node.first("PlayableSide") or "No").split()[0].lower() != "yes":
            continue
        side = (node.first("Side") or "").split()[0] if node.first("Side") else ""
        label = (node.first("DisplayName") or "").split()
        name = strings.get(label[0]) if label else None
        out.append({
            "playerTemplate": d.name,
            "side": side,
            "displayName": name or d.name,
            "displayNameLabel": label[0] if label else None,
            "skirmishBuildList": data.build_list(side) is not None,
            "skirmishScripts": ("skirmish" + side).lower() in players,
            "inRuleset": d.key in base_names if base_vfs is not None else None,
            "file": d.file,
        })
    return {"factions": out, "iniErrors": len(data.errors),
            "stringsFrom": strings.source, "files": len(data.files)}


def format_factions(result):
    lines = []
    n = len(result["factions"])
    if not n:
        lines.append("No playable factions found (PlayableSide = Yes).")
    else:
        lines.append("%d playable faction%s (%d INI files read):" % (n, "" if n == 1 else "s", result["files"]))
    for f in result["factions"]:
        flags = []
        flags.append("build list" if f["skirmishBuildList"] else "no build list")
        flags.append("scripts" if f["skirmishScripts"] else "no scripts")
        if f["inRuleset"] is not None:
            flags.append("also in the ruleset" if f["inRuleset"] else "new in the mod")
        lines.append("%-34s side %-26s \"%s\"  [%s]" % (f["playerTemplate"], f["side"], f["displayName"],
                                                        ", ".join(flags)))
    return "\n".join(lines) + "\n"
