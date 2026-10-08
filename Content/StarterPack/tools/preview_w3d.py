#!/usr/bin/env python3
"""Render a contact sheet of W3D models (flat shaded, painter's algorithm) to a PNG, for eyeballing generated art.

    python3 preview_w3d.py out.png model.w3d [more.w3d ...] [--palette sp_iw.tga]

Needs Pillow (a developer convenience only; the pack build itself needs nothing but Python 3).
"""

import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from spk.w3d import iter_chunks  # noqa: E402
from spk.tga import read_tga  # noqa: E402


def load_meshes(data):
    """-> list of (name, positions, normals, uvs, triangles, has_texture)"""
    out = []

    def walk(blob, pos, end):
        for cid, off, size, _c in iter_chunks(blob, pos, end):
            if cid == 0x00:
                out.append(parse_mesh(blob, off, off + size))

    walk(data, 0, len(data))
    return out


def parse_mesh(blob, start, end):
    name, pos, nrm, uvs, tris, tex = "", [], [], [], [], False
    for cid, off, size, _c in iter_chunks(blob, start, end):
        if cid == 0x1F:
            name = blob[off + 8:off + 24].split(b"\0")[0].decode()
        elif cid == 0x02:
            pos = [struct.unpack_from("<3f", blob, off + i * 12) for i in range(size // 12)]
        elif cid == 0x03:
            nrm = [struct.unpack_from("<3f", blob, off + i * 12) for i in range(size // 12)]
        elif cid == 0x20:
            tris = [struct.unpack_from("<3I", blob, off + i * 32) for i in range(size // 32)]
        elif cid == 0x38:
            for sid, soff, ssize, _c in iter_chunks(blob, off, off + size):
                if sid == 0x48:
                    tex = True
                    for tid, toff, tsize, _c in iter_chunks(blob, soff, soff + ssize):
                        if tid == 0x4A:
                            uvs = [struct.unpack_from("<2f", blob, toff + i * 8) for i in range(tsize // 8)]
    return name, pos, nrm, uvs, tris, tex


def render(meshes, palette, size=240, yaw=0.6, pitch=0.9, team=(70, 130, 210)):
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (size, size), (40, 48, 60))
    d = ImageDraw.Draw(img)
    polys = []
    allp = [p for m in meshes for p in m[1]]
    if not allp:
        return img
    cx = sum(p[0] for p in allp) / len(allp)
    cy = sum(p[1] for p in allp) / len(allp)
    cz = (max(p[2] for p in allp) + min(p[2] for p in allp)) / 2
    extent = max(max(abs(p[0] - cx), abs(p[1] - cy), abs(p[2] - cz)) for p in allp) or 1
    scale = size * 0.42 / extent
    cyaw, syaw, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    light = (-0.4, -0.5, 0.75)
    for name, pos, nrm, uvs, tris, tex in meshes:
        for a, b, c in tris:
            pts, depth = [], 0
            for i in (a, b, c):
                x, y, z = pos[i][0] - cx, pos[i][1] - cy, pos[i][2] - cz
                x, y = x * cyaw - y * syaw, x * syaw + y * cyaw
                sy = y * sp + z * cp
                depth += y * cp - z * sp
                pts.append((size / 2 + x * scale, size / 2 - sy * scale))
            n = nrm[a]
            lam = max(0.15, min(1.0, n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) * 0.8 + 0.3)
            if tex and uvs and palette:
                cell = min(15, max(0, int(uvs[a][0] * 16)))
                base = palette[cell]
            else:
                base = team if name.upper().startswith("HOUSECOLOR") else (200, 200, 200)
            polys.append((depth, pts, tuple(min(255, int(v * lam)) for v in base)))
    for _depth, pts, col in sorted(polys, key=lambda p: -p[0]):
        d.polygon(pts, fill=col)
    return img


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    out = argv[1]
    palette_file = None
    files = []
    i = 2
    while i < len(argv):
        if argv[i] == "--palette":
            palette_file = argv[i + 1]
            i += 2
        else:
            files.append(argv[i])
            i += 1
    palette = None
    if palette_file:
        w, h, px = read_tga(open(palette_file, "rb").read())
        palette = [tuple(px[(4 * (c * 8 + 4)):(4 * (c * 8 + 4)) + 3]) for c in range(16)]
    from PIL import Image
    cols = min(4, len(files))
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 240, rows * 240))
    for n, f in enumerate(files):
        sheet.paste(render(load_meshes(open(f, "rb").read()), palette), ((n % cols) * 240, (n // cols) * 240))
    sheet.save(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
