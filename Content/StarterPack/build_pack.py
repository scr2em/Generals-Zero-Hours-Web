#!/usr/bin/env python3
"""Build the free starter content pack.

    python3 build_pack.py <out-dir> [--big] [--no-generate]

Produces ``<out-dir>/StarterPack/`` laid out like a game install directory (all paths lower case,
because the browser file system layer is case insensitive on top of lower case names), plus
``manifest.json`` which the web launcher downloads. The result is deterministic: building twice
gives byte identical files (no timestamps, fixed seeds).

Everything is original and GPL-3.0-or-later: hand written text under ``data/`` plus the files that
the generators in ``gen/`` synthesise (textures, models, sounds, maps, menus).
"""

import argparse
import hashlib
import importlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))
sys.path.insert(0, HERE)

PACK_VERSION = 1
TEXT_EXTENSIONS = {".ini", ".str", ".wnd", ".txt", ".json"}


def generators():
    """Generator modules in ``gen/``, in a fixed order. Each exposes ``generate(emit)``."""
    gen_dir = os.path.join(HERE, "gen")
    names = sorted(f[:-3] for f in os.listdir(gen_dir) if f.endswith(".py") and not f.startswith("_"))
    for name in names:
        yield importlib.import_module("gen." + name)


def collect(generate=True):
    """Return {install-relative lower case path: bytes}."""
    files = {}

    def emit(path, data):
        key = path.replace("\\", "/").lower().lstrip("/")
        if key in files:
            raise ValueError("generated twice: " + key)
        if isinstance(data, str):
            data = data.encode("latin-1")
        files[key] = bytes(data)

    data_root = os.path.join(HERE, "data")
    for dirpath, dirnames, filenames in os.walk(data_root):
        dirnames.sort()
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, data_root)
            with open(full, "rb") as f:
                raw = f.read()
            if os.path.splitext(name)[1].lower() in (".ini", ".str", ".wnd", ".txt"):
                raw = raw.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            emit(rel, raw)
    if generate:
        for module in generators():
            module.generate(emit)
    return files


def write_tree(files, out_root):
    if os.path.isdir(out_root):
        shutil.rmtree(out_root)
    for rel, data in sorted(files.items()):
        dest = os.path.join(out_root, *rel.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)


def write_manifest(files, out_root, big_name=None):
    entries = [{"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
               for rel, data in sorted(files.items())]
    manifest = {
        "name": "Free starter content",
        "version": PACK_VERSION,
        "license": "GPL-3.0-or-later",
        "description": "Original placeholder game data: one faction, one skirmish map. Not Command & Conquer content.",
        "totalSize": sum(e["size"] for e in entries),
        "files": entries,
    }
    with open(os.path.join(out_root, "manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
        f.write("\n")
    return manifest


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", help="output directory; the pack goes to <out>/StarterPack")
    ap.add_argument("--big", action="store_true", help="also write starterpack.big (all files in one archive)")
    ap.add_argument("--no-generate", action="store_true", help="only copy the hand written data files")
    args = ap.parse_args(argv)

    files = collect(generate=not args.no_generate)
    out_root = os.path.join(os.path.abspath(args.out), "StarterPack")
    write_tree(files, out_root)
    if args.big:
        from spk.bigfile import BigWriter
        big = BigWriter()
        for rel, data in sorted(files.items()):
            big.add(rel.replace("/", "\\"), data)
        big.write(os.path.join(out_root, "..", "StarterPack.big"))
    manifest = write_manifest(files, out_root)
    print("StarterPack: %d files, %d bytes -> %s" % (len(files), manifest["totalSize"], out_root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
