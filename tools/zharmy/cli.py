"""Command line: ``python3 -m zharmy <inspect|convert|validate> ...`` (run from the ``tools`` folder)."""

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
    from .inspect_mod import format_factions, inspect_factions
    res = inspect_factions(a.mod, a.base, a.language, a.mod_archives, _progress(a.quiet))
    if a.json:
        print(json.dumps(res, indent=2))
    else:
        sys.stdout.write(format_factions(res))
        if res["iniErrors"]:
            sys.stdout.write("(%d INI problems while reading; run convert for details)\n" % res["iniErrors"])
    return 0


def cmd_convert(a):
    from .convert import ConvertError, Options, convert, format_report
    opts = Options(tag=a.tag, faction=a.faction, requires=a.requires, id=a.id, name=a.name, version=a.version,
                   description=a.description, authors=a.author or [], source_mod=a.mod_name,
                   source_version=a.mod_version, source_url=a.mod_url, language=a.language,
                   zhc_names=a.zhc_names)
    if a.license:
        opts.license = a.license
    try:
        _c, report = convert(a.mod, a.base, opts, a.output, _progress(a.quiet), a.mod_archives)
    except ConvertError as exc:
        sys.stderr.write("zharmy: error: %s\n" % exc)
        return 2
    sys.stdout.write(format_report(report))
    if a.report:
        with open(a.report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
    sys.stdout.write("wrote %s\n" % a.output)
    if a.validate:
        from .validate import format_result, validate
        res = validate(a.output, a.base or None, a.zhc_names, (), a.mod_archives)
        sys.stdout.write(format_result(res))
        return 0 if res.ok else 1
    return 0


def cmd_convert_all(a):
    from .convert import ConvertError, convert_all, format_summary
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
                                 template, _progress(a.quiet), a.faction)
    except ConvertError as exc:
        sys.stderr.write("zharmy: error: %s\n" % exc)
        return 2
    sys.stdout.write(format_summary(rows))
    if a.report_dir:
        import os
        os.makedirs(a.report_dir, exist_ok=True)
        for r in rows:
            if "report" in r:
                with open(os.path.join(a.report_dir, r["tag"] + ".json"), "w", encoding="utf-8") as h:
                    json.dump(r["report"], h, indent=2)
                    h.write("\n")
    ok = all("error" not in r for r in rows)
    if a.validate:
        from .validate import format_result, validate
        for r in rows:
            if "error" in r:
                continue
            res = validate(r["file"], a.base or None, False, (), a.mod_archives)
            sys.stdout.write(format_result(res))
            ok = ok and res.ok
    return 0 if ok else 1


def cmd_archives(a):
    import os
    from .vfs import glob_matcher, norm
    from .bigfile import BigArchive, BigError
    match = glob_matcher(a.mod_archives) if a.mod_archives else None
    bigs = []
    for dirpath, _d, files in os.walk(a.folder):
        for n in files:
            if n.lower().endswith(".big"):
                full = os.path.join(dirpath, n)
                bigs.append((norm(os.path.relpath(full, a.folder)), full))
    bigs.sort()
    sys.stdout.write("Archives in engine load order (the first archive that holds a file wins):\n")
    for i, (rel, full) in enumerate(bigs, 1):
        try:
            count = len(BigArchive(full).entries)
            note = "%6d files" % count
        except (BigError, OSError) as exc:
            note = "UNREADABLE (%s)" % exc
        role = ""
        if match is not None:
            role = "  MOD" if match(rel) else "  ruleset"
        sys.stdout.write("%3d  %-48s %9.1f MB  %s%s\n" % (i, rel, os.path.getsize(full) / 1048576.0, note, role))
    return 0


def cmd_validate(a):
    from .validate import format_result, validate
    ok = True
    for pkg in a.package:
        res = validate(pkg, a.base or None, a.zhc_names, a.with_, a.mod_archives)
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
                   help="archives (globs on the name or relative path) that belong to the mod when it is installed "
                        "in the --base folder")
    i.add_argument("--language", help="language of the string tables (default: english)")
    i.add_argument("--json", action="store_true")
    i.add_argument("-q", "--quiet", action="store_true")
    i.set_defaults(fn=cmd_inspect)

    c = sub.add_parser("convert", help="convert one faction of a mod into a .zharmy package")
    c.add_argument("mod", nargs="+", help="mod folder(s) and/or .big file(s); later ones win")
    c.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files below the mod")
    c.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                   help="archives (globs on the name or relative path) that belong to the mod when it is installed "
                        "in the --base folder")
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
    c.set_defaults(fn=cmd_convert)

    ca = sub.add_parser("convert-all", help="convert every playable faction of a mod, one package each")
    ca.add_argument("mod", nargs="+", help="mod folder(s) and/or .big file(s); later ones win")
    ca.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files below the mod")
    ca.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                    help="archives (globs on the name or relative path) that belong to the mod when it is "
                         "installed in the --base folder")
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
    ca.set_defaults(fn=cmd_convert_all)

    ar = sub.add_parser("archives", help="list the .big files of a game folder in the order the engine loads them")
    ar.add_argument("folder")
    ar.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                    help="mark the archives these patterns match as the mod's")
    ar.set_defaults(fn=cmd_archives)

    v = sub.add_parser("validate", help="check a package against the rules of docs/ARMY_PACKAGES.md")
    v.add_argument("package", nargs="+")
    v.add_argument("--base", nargs="*", default=[], help="ruleset data folder(s) / .big files")
    v.add_argument("--with", dest="with_", action="append", default=[],
                   help="another package that is loaded together (shadowing and tag checks); repeatable")
    v.add_argument("--mod-archives", nargs="*", default=None, metavar="GLOB",
                   help="archives in the --base folder that are not part of the ruleset (mod installed in the game "
                        "folder)")
    v.add_argument("--zhc-names", action="store_true", help=argparse.SUPPRESS)   # accepted, always on
    v.add_argument("--json", action="store_true")
    v.set_defaults(fn=cmd_validate)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
