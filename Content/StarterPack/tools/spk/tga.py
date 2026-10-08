"""TGA reader/writer (uncompressed 24/32 bit true colour).

The engine's texture loader (``Targa::Load``) and the terrain tile reader
(``WorldHeightMap::readTiles``) accept image type 2 (and RLE type 10) with 24
or 32 bits per pixel. The writer emits uncompressed images.

``origin="top"`` sets the descriptor's top-left bit; ``Targa::Load`` flips
such files into its canonical bottom-left layout. Terrain tile sheets are read
raw by ``readTiles`` (it ignores the origin bit), so they are written with
``origin="bottom"``, i.e. rows in file order are bottom-up like the original
art pipeline produced.
"""

import struct


def write_tga(width, height, rgba, alpha=True, origin="top"):
    """Serialise ``rgba`` (bytes-like, width*height*4, top row first) as a TGA."""
    if len(rgba) != width * height * 4:
        raise ValueError("pixel buffer has the wrong size")
    if origin not in ("top", "bottom"):
        raise ValueError("origin must be 'top' or 'bottom'")
    rgba = bytes(rgba)
    if origin == "bottom":
        stride = width * 4
        rows = [rgba[y * stride:(y + 1) * stride] for y in range(height)]
        rgba = b"".join(reversed(rows))
    depth = 32 if alpha else 24
    descriptor = (8 if alpha else 0) | (0x20 if origin == "top" else 0)
    # id length, colour map type, image type (2 = true colour), colour map spec
    # (first entry, length, entry size), x origin, y origin, width, height,
    # bits per pixel, descriptor.
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, depth, descriptor)
    assert len(header) == 18
    if alpha:
        body = bytearray(len(rgba))
        body[0::4] = rgba[2::4]
        body[1::4] = rgba[1::4]
        body[2::4] = rgba[0::4]
        body[3::4] = rgba[3::4]
    else:
        body = bytearray(width * height * 3)
        body[0::3] = rgba[2::4]
        body[1::3] = rgba[1::4]
        body[2::3] = rgba[0::4]
    return header + bytes(body)


def read_tga(data):
    """Parse a TGA (type 2 or RLE type 10).

    Returns ``(width, height, rgba)`` with the top row first.
    """
    id_len, cmap_type, img_type = struct.unpack_from("<BBB", data, 0)
    width, height, depth, descriptor = struct.unpack_from("<HHBB", data, 12)
    if cmap_type != 0 or img_type not in (2, 10):
        raise ValueError("unsupported TGA type %d" % img_type)
    bpp = depth // 8
    if bpp not in (3, 4):
        raise ValueError("unsupported TGA depth %d" % depth)
    pos = 18 + id_len
    count = width * height
    if img_type == 2:
        raw = bytes(data[pos:pos + count * bpp])
    else:
        out = bytearray()
        while len(out) < count * bpp:
            flag = data[pos]
            pos += 1
            n = (flag & 0x7F) + 1
            if flag & 0x80:
                out += bytes(data[pos:pos + bpp]) * n
                pos += bpp
            else:
                out += data[pos:pos + n * bpp]
                pos += n * bpp
        raw = bytes(out)
    if len(raw) != count * bpp:
        raise ValueError("truncated TGA")
    rgba = bytearray(count * 4)
    rgba[0::4] = raw[2::bpp]
    rgba[1::4] = raw[1::bpp]
    rgba[2::4] = raw[0::bpp]
    rgba[3::4] = raw[3::bpp] if bpp == 4 else b"\xff" * count
    if not descriptor & 0x20:  # bottom-left origin: flip rows to top-first
        stride = width * 4
        rows = [bytes(rgba[y * stride:(y + 1) * stride]) for y in range(height)]
        rgba = bytearray(b"".join(reversed(rows)))
    return width, height, bytes(rgba)
