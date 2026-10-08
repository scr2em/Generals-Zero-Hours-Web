#!/usr/bin/env python3
"""List the INI files and folders the engine loads at start up, and check a pack against them.

    python3 engine_requirements.py [pack dir]  [--repo <repo root>]

Every ``INI::loadFileDirectory("Data\\INI\\Name", ...)`` call (and every ``initSubsystem`` path) throws
``INI_CANT_OPEN_FILE`` unless at least one ``Name.ini`` (or a ``Name/*.ini`` file) exists, so the pack must supply
each of them, even when it is a one line comment. The list is read from the engine sources, so it follows the code.
Entries only used by debug builds (``...Debug``) or by demo builds are reported as optional.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from spk.engine_schema import find_repo_root  # noqa: E402

SOURCE_DIRS = ["Core/GameEngine/Source", "GeneralsMD/Code/GameEngine/Source", "Core/GameEngineDevice/Source",
               "GeneralsMD/Code/GameEngineDevice/Source"]
LITERAL = re.compile(r'"(Data\\\\(?:INI|english)\\\\[A-Za-z0-9_\\\\]+)"')
OPTIONAL_PATTERN = re.compile(r"Debug|Demo|temporary|MapCache|Options", re.I)


def engine_ini_paths(repo):
    """{normalised lower case path without extension: [source files]}"""
    found = {}
    for d in SOURCE_DIRS:
        for dirpath, _dirs, names in os.walk(os.path.join(repo, d)):
            for n in names:
                if not n.endswith(".cpp"):
                    continue
                path = os.path.join(dirpath, n)
                with open(path, encoding="latin-1") as f:
                    text = re.sub(r"//[^\n]*", "", f.read())
                if "loadFileDirectory" not in text and "initSubsystem" not in text:
                    continue
                for m in LITERAL.finditer(text):
                    key = m.group(1).replace("\\\\", "/").lower()
                    found.setdefault(key, set()).add(n)
    return found


def pack_has(files, key):
    if key + ".ini" in files:
        return True
    prefix = key + "/"
    return any(f.startswith(prefix) and f.endswith(".ini") for f in files)


def main(argv=None):
    args = [a for a in (argv if argv is not None else sys.argv[1:])]
    repo = None
    if "--repo" in args:
        i = args.index("--repo")
        repo = args[i + 1]
        del args[i:i + 2]
    repo = repo or find_repo_root()
    required = engine_ini_paths(repo)
    pack = args[0] if args else None
    files = set()
    if pack:
        if os.path.isdir(os.path.join(pack, "StarterPack")):
            pack = os.path.join(pack, "StarterPack")
        for dirpath, _d, names in os.walk(pack):
            for n in names:
                files.add(os.path.relpath(os.path.join(dirpath, n), pack).replace(os.sep, "/").lower())
    missing = 0
    for key in sorted(required):
        optional = bool(OPTIONAL_PATTERN.search(key))
        status = ""
        if pack:
            ok = pack_has(files, key)
            status = "present" if ok else ("MISSING (optional)" if optional else "MISSING")
            if not ok and not optional:
                missing += 1
        print("%-44s %-20s %s" % (key, status, ", ".join(sorted(required[key]))))
    if pack:
        print("%d paths, %d required ones missing" % (len(required), missing))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
