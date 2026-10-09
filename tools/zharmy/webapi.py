"""Entry points for the in-browser importer (``web/armyimport-worker.js`` runs this under Pyodide).

The command line does not use this module. It wraps the same code the commands run (``convert.Context`` /
``Converter``, ``inspect_mod``, ``validate``) in two calls that return plain JSON text:

* ``open_session(...)``  reads the mod (and the ruleset) once and lists the playable factions;
* ``Session.convert(...)`` converts the factions the player ticked from that same data, validates each package and
  returns one row per faction (the numbers of the summary table, the report text, the validation result).

Paths are the ones of the worker's file system: the player's folders are mounted read-only below ``/mnt``.
"""

import glob
import json
import os
import re
import time
import traceback

from . import __version__
from . import layout, strings as stringsmod
from .convert import (TAG_RE, ConvertError, Context, Converter, Options, Unplayable, derive_tags, format_report,
                      unplayable_reason)
from .validate import format_result, validate


def _glob_exact(rel_paths):
    """Glob patterns that match exactly these paths (relative to the game folder)."""
    return [glob.escape(p.replace("\\", "/")) for p in rel_paths]


def _clean(text):
    return re.sub(r"\s+", " ", str(text)).strip()


def describe_error(exc):
    """The message the player sees for an exception: the converter's own text for its errors, else the last line of
    the traceback with the kind of error."""
    if isinstance(exc, ConvertError):
        return str(exc)
    where = ""
    tb = traceback.extract_tb(exc.__traceback__)
    if tb:
        last = tb[-1]
        where = " (%s, line %d)" % (os.path.basename(last.filename), last.lineno)
    return "%s: %s%s" % (type(exc).__name__, exc, where)


def describe_factions(ctx):
    """The playable factions of a loaded ``convert.Context``, like ``inspect_mod.inspect_factions`` lists them (plus how many
    objects the side has), without reading the mod a second time."""
    scb, _problem = ctx.skirmish_scb()
    players = {n.lower() for n, _d in scb.players} if scb else set()
    base_names = None
    if ctx.base is not None:
        base_names = {d.key for d in ctx.base.defs.get("PlayerTemplate", {}).values()}
    per_side = {}
    for d in ctx.mod.defs.get("Object", {}).values():
        side = (d.node.first("Side") or "").strip().lower()
        if side:
            per_side[side] = per_side.get(side, 0) + 1
    out = []
    for d in ctx.mod.ordered("PlayerTemplate"):
        node = d.node
        if (node.first("PlayableSide") or "No").split()[0].lower() != "yes":
            continue
        side = (node.first("Side") or "").split()[0] if node.first("Side") else ""
        label = (node.first("DisplayName") or "").split()
        name = ctx.mod_strings.get(label[0]) if label else None
        out.append({
            "playerTemplate": d.name,
            "side": side,
            "displayName": name or d.name,
            "displayNameLabel": label[0] if label else None,
            "skirmishBuildList": ctx.mod.build_list(side) is not None,
            "skirmishScripts": ("skirmish" + side).lower() in players,
            "unplayable": unplayable_reason(ctx.mod, d),
            "inRuleset": d.key in base_names if base_names is not None else None,
            "objects": per_side.get(side.lower(), 0),
            "file": d.file,
        })
    return out


class _RenamedConverter(Converter):
    """A converter whose army has a name chosen by the player: the faction's name in the game (the string its template's
    DisplayName label points to) and in the manifest."""

    display_name = None

    def _plan_strings(self):
        super()._plan_strings()
        if not self.display_name:
            return
        label = (self.tmpl.node.first("DisplayName") or "").split()
        if label:
            new = self.labelmap.get(label[0].lower()) or "%s:%s" % (self.tag, label[0])
            self.labelmap[label[0].lower()] = new
            _t, bad = stringsmod.to_latin1(self.display_name)
            if bad:
                self.rep.warn("the name has %d character(s) outside Latin-1; replaced with '?'" % bad)
            self.string_entries[new] = self.display_name

    def _display_name(self):
        return self.display_name or super()._display_name()


class Session:
    def __init__(self, ctx, requires, mod_paths, base_paths, mod_archives, mod_name, progress):
        self.ctx = ctx
        self.requires = requires
        self.mod_paths = mod_paths
        self.base_paths = base_paths
        self.mod_archives = mod_archives
        self.mod_name = mod_name
        self.progress = progress
        self.factions = describe_factions(ctx)
        names = [f["playerTemplate"] for f in self.factions]
        tags = derive_tags(names) if names else []
        for f, tag in zip(self.factions, tags):
            f["suggestedTag"] = tag
            f["suggestedName"] = f["displayName"]
        # how much of the problem the player may want to know about
        self.warnings = [w[1] if isinstance(w, tuple) else w for w in ctx.warnings]
        self.notes = list(ctx.notes)

    def info(self):
        return {"factions": self.factions, "warnings": self.warnings, "notes": self.notes, "iniErrors": len(self.ctx.mod.errors),
                "iniFiles": len(self.ctx.mod.files), "stringsFrom": self.ctx.mod_strings.source,
                "requires": self.requires}

    def convert(self, jobs, out_dir, mod_version=None, mod_url=None, description=None, mod_name=None):
        """``jobs``: [{playerTemplate, tag, id, name}]. Writes ``<out_dir>/<id>.zharmy`` for each and returns rows."""
        os.makedirs(out_dir, exist_ok=True)
        ctx = self.ctx
        rows = []
        for n, job in enumerate(jobs, 1):
            tag = job["tag"]
            row = {"faction": job["playerTemplate"], "tag": tag, "id": job["id"], "ok": False}
            rows.append(row)
            t0 = time.time()
            self.progress("[%d/%d] %s -> tag %s" % (n, len(jobs), job["playerTemplate"], tag))
            out = os.path.join(out_dir, job["id"] + ".zharmy")
            try:
                if not TAG_RE.match(tag):
                    raise ConvertError("The tag %r is not valid: 2 to 6 capital letters or digits, starting with a "
                                       "letter." % tag)
                name = (job.get("name") or "").strip() or None
                original = next((f["displayName"] for f in self.factions if f["playerTemplate"] == job["playerTemplate"]), None)
                opts = Options(tag=tag, faction=job["playerTemplate"], requires=self.requires, id=job["id"],
                               name=name, source_mod=mod_name or self.mod_name,
                               source_version=mod_version or None, source_url=mod_url or None,
                               description=description or None)
                conv = _RenamedConverter(ctx, opts, self.progress)
                conv.display_name = name if name and name != original else None
                report = conv.run(out)
                self.progress("checking the package")
                res = validate(out, world=ctx)
                m = conv.manifest
                asset = lambda kinds: len({r["path"] for r in report["assets"]["copied"] if r["kind"] in kinds})
                row.update({
                    "ok": True,
                    "valid": res.ok,
                    "file": out,
                    "name": m["name"],
                    "sizeMB": os.path.getsize(out) / 1048576.0,
                    "objects": len(report["definitionsCopied"].get("Object", [])),
                    "weapons": len(report["definitionsCopied"].get("Weapon", [])),
                    "models": asset(("Model",)),
                    "textures": asset(("Texture",)),
                    "sounds": asset(("AudioFile", "TrackFile", "SpeechFile")),
                    "warnings": list(report["warnings"]),
                    "validation": format_result(res),
                    "validationErrors": ["[%s] %s" % (r, msg) for r, msg in res.errors],
                    "report": format_report(report),
                    "ai": bool(m["factions"][0].get("ai")),
                })
            except Exception as exc:                      # a conversion that fails must not stop the others
                row["ok"] = False
                row["error"] = _clean(describe_error(exc)) if isinstance(exc, ConvertError) else describe_error(exc)
                if os.path.exists(out):
                    try:
                        os.remove(out)
                    except OSError:
                        pass
            row["seconds"] = time.time() - t0
        return rows


def open_session(mod_paths, base_paths, requires, mod_archives, mod_name, language, progress):
    """Reads the mod. ``requires``: 'zerohour' or 'none'. ``mod_archives``: relative paths of the archives that are
    the mod's when the mod is installed in the ``base_paths`` folder, or ``"auto"`` (every archive without a retail file
    name, the command line's default for that case); None otherwise."""
    if requires != "none" and not base_paths:
        raise ConvertError("Importing needs your Zero Hour game files to compare the mod with. Choose your Zero Hour "
                           "folder first, or make the armies work without them.")
    if mod_archives == "auto":
        patterns = ["auto"]
    else:
        patterns = _glob_exact(mod_archives) if mod_archives else None
    ctx = Context(list(mod_paths), list(base_paths or []), requires, language or None, patterns, progress)
    return Session(ctx, requires, list(mod_paths), list(base_paths or []), patterns, mod_name, progress)


# ---- the calls the worker makes (JSON text in and out; the session is kept here between calls) ----------------------
_current = None


def open_json(params, progress):
    """``params``: JSON with modPaths, basePaths, requires, modArchives, modName, language. Returns JSON text:
    {ok: true, ...Session.info()} or {ok: false, error: <message for the player>}."""
    global _current
    close()
    try:
        p = json.loads(params)
        _current = open_session(p["modPaths"], p.get("basePaths"), p["requires"], p.get("modArchives"),
                                p.get("modName") or "mod", p.get("language"), progress)
        info = _current.info()
        info["ok"] = True
        return json.dumps(info)
    except Exception as exc:
        _current = None
        return json.dumps({"ok": False, "error": describe_error(exc)})


def convert_json(job, out_dir):
    """``job``: JSON with the fields of one entry of Session.convert plus modVersion / modUrl. Returns the row as JSON."""
    j = json.loads(job)
    if _current is None:
        return json.dumps({"ok": False, "error": "No mod is open."})
    rows = _current.convert([j], out_dir, j.get("modVersion"), j.get("modUrl"), j.get("description"), j.get("modName"))
    return json.dumps(rows[0])


def retail_json(names):
    """For the list of archive paths (JSON), JSON of booleans: is this a retail Zero Hour / Generals archive name?
    (``layout.is_retail_archive``, the list ``--mod-archives auto`` uses.)"""
    return json.dumps([layout.is_retail_archive(n) for n in json.loads(names)])


def close():
    global _current
    _current = None
    import gc
    from . import vfs
    vfs._SOURCES.clear()
    gc.collect()


def version():
    return __version__
