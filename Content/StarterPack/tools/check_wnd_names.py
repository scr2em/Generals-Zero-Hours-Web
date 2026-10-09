#!/usr/bin/env python3
"""Cross-check the pack's WND layouts against the window names the engine source looks up.

    python3 check_wnd_names.py <built pack dir> [--repo <repo root>] [--all]

For every layout of the pack, the engine sources are scanned for ``"Layout.wnd:Name"`` string literals
(and ``format``-style names such as ``"Layout.wnd:ButtonCommand%02d"``). A name that code looks up but the layout
does not define is reported. Many lookups are optional (the code null checks them or the screen is not part of
the starter flow), so the output is a work list, not a verdict; ``--all`` also lists names that are present.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from spk.engine_schema import find_repo_root  # noqa: E402
from spk.wnd import parse_wnd  # noqa: E402

SOURCE_DIRS = ["Core/GameEngine/Source", "GeneralsMD/Code/GameEngine/Source", "Core/GameEngineDevice/Source",
               "GeneralsMD/Code/GameEngineDevice/Source"]


def engine_references(repo):
    refs = {}  # layout (lower) -> {name: [files]}
    pat = re.compile(r'"([A-Za-z0-9_]+\.wnd):([A-Za-z0-9_%]+)"')
    for d in SOURCE_DIRS:
        for dirpath, _dirs, files in os.walk(os.path.join(repo, d)):
            for f in files:
                if not f.endswith(".cpp"):
                    continue
                path = os.path.join(dirpath, f)
                text = open(path, encoding="latin-1").read()
                text = re.sub(r"//[^\n]*", "", text)
                for m in pat.finditer(text):
                    refs.setdefault(m.group(1).lower(), {}).setdefault(m.group(2), set()).add(f)
    return refs


def layout_names(data):
    _layout, windows = parse_wnd(data)
    names = set()
    for w in windows:
        for node in w.walk():
            if node.name and ":" in node.name:
                names.add(node.name.split(":", 1)[1])
    return names


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("pack", help="built pack directory (contains window/...)")
    ap.add_argument("--repo", default=None)
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args(argv)
    repo = args.repo or find_repo_root()
    refs = engine_references(repo)
    missing_total = 0
    for dirpath, _dirs, files in os.walk(os.path.join(args.pack, "window")):
        for f in sorted(files):
            if not f.endswith(".wnd"):
                continue
            have = layout_names(open(os.path.join(dirpath, f), "rb").read())
            wanted = refs.get(f.lower(), {})
            missing = []
            for name, where in sorted(wanted.items()):
                if "%" in name:
                    stem = re.sub(r"%\d*d", "", name)
                    ok = any(re.fullmatch(re.escape(stem) + r"\d+", h) for h in have)
                else:
                    ok = name in have
                if not ok:
                    missing.append((name, sorted(where)))
                elif args.all:
                    print("  ok      %s:%s" % (f, name))
            print("%s: %d windows, %d names looked up by code, %d missing" % (f, len(have), len(wanted), len(missing)))
            for name, where in missing:
                print("  MISSING %s   (%s)" % (name, ", ".join(where[:3])))
            missing_total += len(missing)
    return 0


if __name__ == "__main__":
    sys.exit(main())
