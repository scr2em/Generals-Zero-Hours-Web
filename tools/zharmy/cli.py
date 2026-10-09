"""Command line: ``python3 -m zharmy <archives|find|inspect|convert|convert-all|validate> ...`` (run from the
``tools`` folder)."""

import argparse
import json
import sys
import time

from . import __version__


def _progress(quiet):
    start = time.time()

    def p(msg):
        if not quiet:
            sys.stderr.write("zharmy [%5.1fs]: %s\n" % (time.time() - start, msg))
            sys.stderr.flush()
    return p


def cmd_inspect(a):
    from .convert import ConvertError
    from .inspect_mod import format_factions, inspect_factions
    try:
        res = inspect_factions(a.mod, a.base, a.language, a.mod_archives, _progress(a.quiet), a.loose)
    except ConvertError as exc:
        sys.stderr.write("zharmy: error: %s\n" % exc)
        return 2
    if a.json:
        print(json.dumps(res, indent=2))
    else:
        sys.stdout.write(format_factions(res, a.ini_problems))
    return 0


def cmd_convert(a):
    from .convert import ConvertError, Options, convert, format_report, format_setup
    opts = Options(tag=a.tag, faction=a.faction, requires=a.requires, id=a.id, name=a.name, version=a.version,
                   description=a.description, authors=a.author or [], source_mod=a.mod_name,
                   source_version=a.mod_version, source_url=a.mod_url, language=a.language,
                   zhc_names=a.zhc_names)
    if a.license:
        opts.license = a.license
    try:
        _c, report = convert(a.mod, a.base, opts, a.output, _progress(a.quiet), a.mod_archives, a.loose)
    except ConvertError as exc:
        sys.stderr.write("zharmy: error: %s\n" % exc)
        return 2
    sys.stdout.write(format_setup(_c.ctx))
    sys.stdout.write(format_report(report))
    if a.report:
        with open(a.report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
    sys.stdout.write("wrote %s\n" % a.output)
    if a.validate:
        from .validate import format_result, validate
        res = validate(a.output, a.base or None, a.zhc_names, (), world=_c.ctx)
        sys.stdout.write(format_result(res))
        return 0 if res.ok else 1
    return 0


def cmd_convert_all(a):
    from .convert import ConvertError, convert_all, format_setup, format_summary
    template = {}
    if a.author:
        template["authors"] = a.author
    if a.license:
        template["license"] = a.license
    for src, dst in (("mod_name", "source_mod"), ("mod_version", "source_version"), ("mod_url", "source_url"),
                     ("description", "description")):
        if getattr(a, src):
            template[dst] = getattr(a, src)
    try:
        rows, _ctx = convert_all(a.mod, a.base, a.out_dir, a.tag_prefix, a.requires, a.language, a.mod_archives,
                                 template, _progress(a.quiet), a.faction, a.loose)
    except ConvertError as exc:
        sys.stderr.write("zharmy: error: %s\n" % exc)
        return 2
    sys.stdout.write(format_setup(_ctx))
    sys.stdout.write(format_summary(rows))
    if a.report_dir:
        import os
        os.makedirs(a.report_dir, exist_ok=True)
        for r in rows:
            if "report" in r:
                with open(os.path.join(a.report_dir, r["tag"] + ".json"), "w", encoding="utf-8") as h:
                    json.dump(r["report"], h, indent=2)
                    h.write("\n")
    ok = all("error" not in r and "cannot" not in r for r in rows)
    if a.validate:
        from .validate import format_result, validate
        for r in rows:
            if "report" not in r:
                continue
            res = validate(r["file"], a.base or None, False, (), world=_ctx)
            sys.stdout.write(format_result(res))
            ok = ok and res.ok
    return 0 if ok else 1


def cmd_archives(a):
    import os
    from . import layout
    from .bigfile import BigArchive, BigError
    folder = a.folder
    lay = layout.detect_layout(folder)
    for line in lay.lines():
        sys.stdout.write(line + "\n")
    patterns = a.mod_archives if a.mod_archives is not None else ["auto"]
    error = None
    sel = None
    try:
        sel = layout.resolve_mod_archives([folder], patterns, True, a.loose)
    except layout.SelectionError as exc:
        error = str(exc)
    pred = sel.predicate() if sel is not None else None
    sys.stdout.write("Archives in engine load order (the first archive that holds a file wins)%s:\n"
                     % ("; MOD = would be taken as the mod's by --mod-archives %s" % " ".join(patterns)))
    for i, arc in enumerate(layout.list_archives(folder), 1):
        try:
            note = "%6d files" % len(BigArchive(arc.full).entries)
        except (BigError, OSError) as exc:
            note = "UNREADABLE (%s)" % exc
        role = "  MOD" if pred is not None and pred(arc.rel) else "  ruleset"
        sys.stdout.write("%3d  %-56s %9.1f MB  %s  %s%s\n" % (
            i, arc.rel, os.path.getsize(arc.full) / 1048576.0, note, "retail name" if arc.retail else "no retail name",
            role))
    if sel is not None and sel.active:
        sys.stdout.write("%d archive(s) would be taken as the mod's.\n" % len(sel.picked))
        for line in layout.loose_lines(sel):
            sys.stdout.write(line + "\n")
        if sel.loose_files:
            sys.stdout.write("Loose files treated as the mod's, first ones: %s\n"
                             % ", ".join(rel for _f, rel in sel.loose_files[:5]))
    if error:
        sys.stderr.write("zharmy: %s\n" % ("error: " + error))
        return 2
    return 0


def cmd_find(a):
    import fnmatch
    import os
    from . import layout
    from .vfs import Vfs, norm
    if len(a.args) < 2:
        sys.stderr.write("zharmy: error: find needs at least one folder and a glob, e.g. find <folder> "
                         "'*skirmishscripts*'\n")
        return 2
    folders, pattern = a.args[:-1], norm(a.args[-1]).replace("\\", "/")
    vfs = Vfs()
    for f in folders:
        if os.path.isdir(f):
            layout.add_game_tree(vfs, f, overwrite=False)
        else:
            vfs.add_tree(f, overwrite=False)

    def hit(path):
        return fnmatch.fnmatchcase(path, pattern.lower()) or fnmatch.fnmatchcase(path.rsplit("/", 1)[-1],
                                                                                  pattern.lower())
    found = {}
    for layer in reversed(vfs.layers):         # highest priority first
        for key, handle in layer.index.items():
            if hit(key):
                found.setdefault(key, []).append((layer, handle))
    shown = 0
    for key in sorted(found):
        if shown >= a.limit:
            sys.stdout.write("... %d more paths (raise --limit)\n" % (len(found) - shown))
            break
        shown += 1
        sys.stdout.write("%s\n" % key)
        for i, (layer, handle) in enumerate(found[key]):
            label = layer.label
            for f in folders:
                if os.path.isabs(label) or os.path.exists(label):
                    try:
                        rel = os.path.relpath(label, f)
                        if not rel.startswith(".."):
                            label = rel
                            break
                    except ValueError:
                        pass
            kind = "loose file" if hasattr(layer, "folder") else "archive"
            inner = "" if hasattr(layer, "folder") else "  as %s" % handle
            sys.stdout.write("   %s %-10s %s%s\n" % ("->" if i == 0 else "  ", kind, label, inner))
    if not found:
        sys.stdout.write("no path matches %r in %d archive(s)/folder(s) (paths are case-insensitive; * ? [] are "
                         "allowed; the pattern is matched against the whole path or the file name)\n"
                         % (a.args[-1], len(vfs.layers)))
        return 1
    sys.stdout.write("%d path(s); '->' marks the file the engine uses.\n" % len(found))
    return 0


def cmd_validate(a):
    from .validate import format_result, validate
    ok = True
    for pkg in a.package:
        res = validate(pkg, a.base or None, a.zhc_names, a.with_, a.mod_archives, a.mod or None, loose=a.loose)
        if a.json:
            print(json.dumps(res.as_dict(), indent=2))
        else:
            sys.stdout.write(format_result(res))
        ok = ok and res.ok
    return 0 if ok else 1


def build_parser():
    p = argparse.ArgumentParser(prog="zharmy", description="Army packages (.zharmy) for the Zero Hour web port")
    p.add_argument("--version", action="version", version="zharmy " + __version__)
    sub = p.add_subparsers(dest="cmd", required=True)

    i = sub.add_parser("inspect", help="list the playable factions of a mod")
    i.add_argument("mod", nargs="+", help="mod folder(s) and/or .big file(s); later ones win")
    i.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files below the mod")
    i.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                   help="archives (globs on the name or relative path, or 'auto' = every archive without a retail "
                        "file name) that belong to the mod when it is installed in the --base folder; default: "
                        "auto when the mod folder is the --base folder")
    i.add_argument("--language", help="language of the string tables (default: english)")
    i.add_argument("--json", action="store_true")
    i.add_argument("--ini-problems", action="store_true",
                   help="list every INI problem (file:line), not only the summary by kind")
    i.add_argument("-q", "--quiet", action="store_true")
    i.add_argument("--loose", choices=["mod", "ruleset"], default=None,
                    help="loose files in the game folder (Data/, Art/): 'mod' = part of the mod unless identical to "
                         "the retail archive copy (default for a mod installed in the game folder), 'ruleset' = "
                         "retail data")
    i.set_defaults(fn=cmd_inspect)

    c = sub.add_parser("convert", help="convert one faction of a mod into a .zharmy package")
    c.add_argument("mod", nargs="+", help="mod folder(s) and/or .big file(s); later ones win")
    c.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files below the mod")
    c.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                   help="archives (globs on the name or relative path, or 'auto' = every archive without a retail "
                        "file name) that belong to the mod when it is installed in the --base folder; default: "
                        "auto when the mod folder is the --base folder")
    c.add_argument("--faction", required=True, help="PlayerTemplate name, e.g. FactionChinaNuke")
    c.add_argument("--tag", required=True, help="2-6 characters, A-Z and 0-9, starting with a letter")
    c.add_argument("--id", help="package id (default <tag>.<faction>)")
    c.add_argument("--name", help="display name of the package (default: the faction's name)")
    c.add_argument("--requires", choices=["zerohour", "starter", "none"], default="zerohour",
                   help="ruleset the package relies on; none = copy everything")
    c.add_argument("--version", default="1.0.0", help="package version (default 1.0.0)")
    c.add_argument("--description")
    c.add_argument("--author", action="append", help="repeatable")
    c.add_argument("--license", help="license text shown by the launcher")
    c.add_argument("--mod-name")
    c.add_argument("--mod-version")
    c.add_argument("--mod-url")
    c.add_argument("--language", help="language of the string table to copy from (default: english)")
    c.add_argument("--no-zhc-names", dest="zhc_names", action="store_false",
                   help="name house-colour textures <TAG>... like all other files; they lose their team colour "
                        "(default: ZHC<TAG>..., which keeps it)")
    c.add_argument("-o", "--output", required=True)
    c.add_argument("--report", help="also write the report as JSON")
    c.add_argument("--validate", action="store_true", help="validate the result against --base afterwards")
    c.add_argument("-q", "--quiet", action="store_true")
    c.add_argument("--loose", choices=["mod", "ruleset"], default=None,
                    help="loose files in the game folder (Data/, Art/): 'mod' = part of the mod unless identical to "
                         "the retail archive copy (default for a mod installed in the game folder), 'ruleset' = "
                         "retail data")
    c.set_defaults(fn=cmd_convert)

    ca = sub.add_parser("convert-all", help="convert every playable faction of a mod, one package each")
    ca.add_argument("mod", nargs="+", help="mod folder(s) and/or .big file(s); later ones win")
    ca.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files below the mod")
    ca.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                    help="archives (globs on the name or relative path, or 'auto' = every archive without a retail "
                         "file name) that belong to the mod when it is installed in the --base folder; default: "
                         "auto when the mod folder is the --base folder")
    ca.add_argument("--out-dir", required=True)
    ca.add_argument("--tag-prefix", help="tags become PREFIX1, PREFIX2, ... (1-5 letters/digits, starting with a "
                                         "letter); default: derived from the faction names")
    ca.add_argument("--faction", action="append", help="only this PlayerTemplate (repeatable)")
    ca.add_argument("--requires", choices=["zerohour", "starter", "none"], default="zerohour")
    ca.add_argument("--language")
    ca.add_argument("--author", action="append")
    ca.add_argument("--license")
    ca.add_argument("--mod-name")
    ca.add_argument("--mod-version")
    ca.add_argument("--mod-url")
    ca.add_argument("--description")
    ca.add_argument("--report-dir", help="write one JSON report per package here")
    ca.add_argument("--validate", action="store_true")
    ca.add_argument("-q", "--quiet", action="store_true")
    ca.add_argument("--loose", choices=["mod", "ruleset"], default=None,
                    help="loose files in the game folder (Data/, Art/): 'mod' = part of the mod unless identical to "
                         "the retail archive copy (default for a mod installed in the game folder), 'ruleset' = "
                         "retail data")
    ca.set_defaults(fn=cmd_convert_all)

    ar = sub.add_parser("archives", help="list the .big files of a game folder in the order the engine loads them")
    ar.add_argument("folder")
    ar.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                    help="mark the archives these patterns match as the mod's (default: auto, the archives "
                         "without a retail file name)")
    ar.add_argument("--loose", choices=["mod", "ruleset"], default=None,
                    help="loose files in the game folder (Data/, Art/): 'mod' = part of the mod unless identical to "
                         "the retail archive copy (default for a mod installed in the game folder), 'ruleset' = "
                         "retail data")
    ar.set_defaults(fn=cmd_archives)

    fi = sub.add_parser("find", help="list which archive or loose file provides the paths matching a glob")
    fi.add_argument("args", nargs="+", metavar="FOLDER... GLOB",
                    help="one or more game/mod folders (or .big files), then the glob, e.g. "
                         "find \"$ZH\" '*skirmishscripts*'")
    fi.add_argument("--limit", type=int, default=100, help="show at most this many paths (default 100)")
    fi.set_defaults(fn=cmd_find)

    v = sub.add_parser("validate", help="check a package against the rules of docs/ARMY_PACKAGES.md")
    v.add_argument("package", nargs="+")
    v.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files")
    v.add_argument("--with", dest="with_", action="append", default=[],
                   help="another package that is loaded together (shadowing and tag checks); repeatable")
    v.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                   help="archives in the --base folder that are not part of the ruleset (mod installed in the game "
                        "folder): GLOBs or 'auto'. Also tells validate where the mod as played is, so references "
                        "the mod itself lacks are warnings, not errors")
    v.add_argument("--mod", nargs="*", default=[], help="mod folder(s) / .big files on top of --base (the mod as "
                   "played), for the same purpose")
    v.add_argument("--zhc-names", action="store_true", help=argparse.SUPPRESS)   # accepted, always on
    v.add_argument("--json", action="store_true")
    v.add_argument("--loose", choices=["mod", "ruleset"], default=None,
                    help="loose files in the game folder (Data/, Art/): 'mod' = part of the mod unless identical to "
                         "the retail archive copy (default for a mod installed in the game folder), 'ruleset' = "
                         "retail data")
    v.set_defaults(fn=cmd_validate)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
