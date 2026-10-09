"""``zharmy inspect``: list the playable factions of a mod."""

from . import scb as scbmod, strings as stringsmod
from .convert import ConvertError, build_world, unplayable_reason
from .gamedata import GameData, format_ini_problems


def inspect_factions(mod_paths, base_paths=None, language=None, mod_archives=None, progress=None, loose=None):
    progress = progress or (lambda m: None)
    progress("reading archive directories")
    world = build_world(mod_paths, base_paths, mod_archives, loose)
    mod_vfs, base_vfs = world.mod_vfs, world.base_vfs
    notes = list(world.notes)
    for line in notes:
        progress(line)
    progress("loading definitions (%d archives/folders)" % len(mod_vfs.layers))
    data = GameData(mod_vfs, "mod")
    lang, why = stringsmod.choose_language(mod_vfs, base_vfs, language)
    notes.append("String tables: %s (%s)" % (lang or "language independent", why))
    strings = stringsmod.load_strings(mod_vfs, lang)
    scb = None
    scb_note = None
    if mod_vfs.exists("data/scripts/skirmishscripts.scb"):
        try:
            scb = scbmod.read_scb(mod_vfs.read("data/scripts/skirmishscripts.scb"))
        except (scbmod.ScbError, ValueError, IndexError) as exc:
            scb_note = "SkirmishScripts.scb could not be read: %s" % exc
    else:
        scb_note = ("no Data/Scripts/SkirmishScripts.scb in any loose folder or archive searched (zharmy find "
                    "<folder> '*skirmishscripts*' shows what exists)")
    if scb_note:
        notes.append(scb_note)
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
            "unplayable": unplayable_reason(data, d),
            "inRuleset": d.key in base_names if base_vfs is not None else None,
            "file": d.file,
        })
    return {"factions": out, "iniErrors": len(data.errors), "iniProblems": [list(e) for e in data.errors],
            "stringsFrom": strings.source, "language": lang, "files": len(data.files), "notes": notes,
            "layers": len(mod_vfs.layers)}


def format_factions(result, ini_problems=False):
    lines = list(result.get("notes", []))
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
        if f.get("unplayable"):
            flags.append("UNPLAYABLE: " + f["unplayable"])
        lines.append("%-34s side %-26s \"%s\"  [%s]" % (f["playerTemplate"], f["side"], f["displayName"],
                                                        ", ".join(flags)))
    out = "\n".join(lines) + "\n"
    if result["iniErrors"]:
        problems = [tuple(e) for e in result.get("iniProblems", [])]
        if ini_problems:
            out += format_ini_problems(problems, full=True)
        else:
            out += format_ini_problems(problems, examples=3) + "(use --ini-problems to list all)\n"
    return out
