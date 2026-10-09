#!/usr/bin/env python3
"""Writes animated Windows cursors (.ANI) for testing the page's cursor support (web/zhcursor.js).

    make_ani.py <starterpack dir> [name ...]

Adds data/cursors/<name>.ani (lower case, like the pack's other files) to a built starter pack (and to its manifest.json) for every <name> (default: the cursors
that the starter pack's Mouse.ini asks for). The cursors have 4 frames of 32x32 32 bit pixels, a distinct colour per frame
and the hot spot (3, 5), so a test can tell that the frames play in order and that the hot spot survived.
"""
import hashlib
import json
import os
import struct
import sys

SIZE = 32
FRAME_COLORS = [(255, 40, 40), (40, 255, 40), (40, 40, 255), (255, 255, 40)]  # RGB
HOT = (3, 5)


def cur_frame(rgb):
    """One .cur file: ICONDIR + entry + BITMAPINFOHEADER + BGRA pixels (bottom up) + AND mask."""
    r, g, b = rgb
    pixels = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            inside = x < 20 and y < 20 and (x + y) < 24  # an arrow-ish wedge
            pixels += bytes([b, g, r, 255 if inside else 0])
    mask_row = b"\x00" * 4  # 32 bits per row, all opaque (alpha channel does the shaping)
    and_mask = mask_row * SIZE
    header = struct.pack("<IiiHHIIiiII", 40, SIZE, SIZE * 2, 1, 32, 0, len(pixels) + len(and_mask), 0, 0, 0, 0)
    image = header + bytes(pixels) + and_mask
    entry = struct.pack("<BBBBHHII", SIZE, SIZE, 0, 0, HOT[0], HOT[1], len(image), 22)
    return struct.pack("<HHH", 0, 2, 1) + entry + image


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + (b"\x00" if len(data) & 1 else b"")


def ani(frames=FRAME_COLORS, jiffies=6):
    icons = b"".join(chunk(b"icon", cur_frame(c)) for c in frames)
    anih = struct.pack("<IIIIIIIII", 36, len(frames), len(frames), SIZE, SIZE, 32, 1, jiffies, 1)
    body = b"ACON" + chunk(b"anih", anih) + chunk(b"LIST", b"fram" + icons)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def main():
    pack = sys.argv[1]
    names = sys.argv[2:]
    if not names:
        with open(os.path.join(pack, "data", "ini", "mouse.ini"), errors="replace") as f:
            names = [line.split("=")[1].strip() for line in f if line.strip().startswith("Texture")]
    manifest_path = os.path.join(pack, "manifest.json")
    manifest = json.load(open(manifest_path))
    known = {e["path"]: e for e in manifest["files"]}
    for name in names:
        rel = "data/cursors/%s.ani" % name.lower()
        data = ani()
        dest = os.path.join(pack, *rel.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        open(dest, "wb").write(data)
        known[rel] = {"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    manifest["files"] = sorted(known.values(), key=lambda e: e["path"])
    manifest["totalSize"] = sum(e["size"] for e in manifest["files"])
    json.dump(manifest, open(manifest_path, "w"), indent=1, sort_keys=True)
    print("%d cursors added to %s" % (len(names), pack))


if __name__ == "__main__":
    main()
