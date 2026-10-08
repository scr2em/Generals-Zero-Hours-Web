"""Terrain tile sheets, water, sky, particles and the handful of textures the engine loads by fixed name.

Terrain sheets (``Art/Terrain/*.tga``) follow WorldHeightMap::countTiles / readTiles: an uncompressed or RLE
TGA whose size is a multiple of 64 pixels; a 128x128 sheet holds 2x2 = 4 tiles of 64x64 pixels. Every
tile is periodic in both directions so the map can repeat them freely.

Textures the engine asks for by hard coded name (the code falls back to a built in "missing" texture when
they are absent, but a pack should not rely on that): shadow, TBBib, TSNoiseUrb, TSCloudMed, EXScorch01,
cloudmap, alphaclip, wave1/wave2/wave256, missing.
"""

import math

from spk.image import Canvas, mix, shade
from spk.util import Rng, fbm, clamp

TILE = 64


def _tile(base, dark, light, seed, scale=9.0, strength=0.35, speck=None, speck_count=60, blades=False):
    c = Canvas(TILE, TILE, base)
    # two noise layers blend between dark and light
    for y in range(TILE):
        for x in range(TILE):
            n = _periodic_noise(x, y, TILE, scale, seed)
            m = _periodic_noise(x, y, TILE, scale / 3.0, seed + 7)
            t = clamp(0.5 + (n - 0.5) * 1.6 + (m - 0.5) * 0.5, 0.0, 1.0)
            if t < 0.5:
                col = mix(dark, base, t * 2)
            else:
                col = mix(base, light, (t - 0.5) * 2)
            c.set(x, y, col)
    rng = Rng(seed + 100)
    if speck is not None:
        for _ in range(speck_count):
            x, y = rng.randint(0, TILE - 1), rng.randint(0, TILE - 1)
            c.set(x, y, speck)
            if blades:
                c.set(x, (y - 1) % TILE, mix(speck, light, 0.5))
    return c


def _periodic_noise(x, y, size, scale, seed):
    """Value noise that wraps every ``size`` pixels."""
    cells = max(1, int(round(size / scale)))
    u, v = x / size * cells, y / size * cells
    x0, y0 = math.floor(u), math.floor(v)
    fx, fy = u - x0, v - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    from spk.util import hash_noise_2d

    def h(ix, iy):
        return hash_noise_2d(ix % cells, iy % cells, seed)

    a, b, c_, d = h(x0, y0), h(x0 + 1, y0), h(x0, y0 + 1), h(x0 + 1, y0 + 1)
    top = a + (b - a) * fx
    bottom = c_ + (d - c_) * fx
    return top + (bottom - top) * fy


def _sheet(tiles):
    """2x2 sheet from four tiles."""
    s = Canvas(TILE * 2, TILE * 2)
    for i, t in enumerate(tiles):
        s.paste(t, (i % 2) * TILE, (i // 2) * TILE, blend=False)
    return s


def grass_sheet():
    return _sheet([_tile((72, 128, 56), (50, 100, 42), (104, 156, 70), 11 + i, speck=(120, 170, 80), speck_count=90,
                         blades=True) for i in range(4)])


def dirt_sheet():
    return _sheet([_tile((142, 110, 74), (112, 84, 54), (170, 138, 96), 21 + i, scale=8, speck=(96, 70, 44),
                         speck_count=50) for i in range(4)])


def rock_sheet():
    tiles = []
    for i in range(4):
        t = _tile((118, 116, 112), (84, 84, 84), (156, 152, 146), 31 + i, scale=6, speck=(70, 70, 72), speck_count=40)
        # a few cracks
        rng = Rng(300 + i)
        for _ in range(3):
            x, y = rng.randint(0, TILE - 1), rng.randint(0, TILE - 1)
            for _k in range(14):
                t.set(x % TILE, y % TILE, (66, 66, 68))
                x += rng.randint(-1, 1)
                y += rng.randint(0, 1)
        tiles.append(t)
    return _sheet(tiles)


def sand_sheet():
    tiles = []
    for i in range(4):
        t = _tile((206, 186, 130), (184, 162, 106), (226, 208, 156), 41 + i, scale=10, speck=(170, 148, 96), speck_count=40)
        # ripples
        for y in range(TILE):
            for x in range(TILE):
                r = math.sin((y + 5 * math.sin(x * 2 * math.pi / TILE * 2)) * 2 * math.pi / 16.0)
                px = t.get(x, y)
                f = 1.0 + 0.04 * r
                t.set(x, y, (int(px[0] * f), int(px[1] * f), int(px[2] * f), 255))
        tiles.append(t)
    return _sheet(tiles)


def cliff_sheet():
    tiles = []
    for i in range(4):
        t = _tile((100, 92, 84), (62, 58, 54), (136, 126, 114), 51 + i, scale=5, strength=0.5)
        for y in range(TILE):
            for x in range(TILE):
                # horizontal strata
                s = math.sin((y + 4 * _periodic_noise(x, y, TILE, 8, 77 + i)) * 2 * math.pi / 16.0)
                px = t.get(x, y)
                f = 1.0 + 0.12 * s
                t.set(x, y, (int(px[0] * f), int(px[1] * f), int(px[2] * f), 255))
        tiles.append(t)
    return _sheet(tiles)


def sky_texture():
    c = Canvas(128, 128)
    c.gradient_v(0, 0, 128, 128, (96, 150, 214), (190, 220, 244))
    rng = Rng(9)
    for _ in range(26):
        cx, cy = rng.randint(0, 127), rng.randint(10, 100)
        r = rng.randint(6, 14)
        for dy in range(-r // 2, r // 2 + 1):
            for dx in range(-r, r + 1):
                d = (dx / r) ** 2 + (dy / (r / 2.0)) ** 2
                if d < 1:
                    c.blend((cx + dx) % 128, cy + dy, (255, 255, 255, int(110 * (1 - d))))
    return c


def water_texture():
    c = Canvas(128, 128, (40, 96, 150, 255))
    for y in range(128):
        for x in range(128):
            n = _periodic_noise(x, y, 128, 16, 5)
            m = _periodic_noise(x, y, 128, 6, 6)
            t = clamp(0.5 + (n - 0.5) * 1.4 + (m - 0.5) * 0.4, 0, 1)
            c.set(x, y, mix((30, 84, 140), (90, 150, 200), t))
    return c


def smoke_texture():
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5) / 32.0
            n = fbm(x / 9.0, y / 9.0, 3, 17)
            a = clamp((1 - d) * 1.4 * (0.55 + 0.9 * n), 0, 1)
            c.set(x, y, (200, 200, 200, int(255 * a * a)))
    return c


def flame_texture():
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5) / 32.0
            n = fbm(x / 8.0, y / 8.0, 3, 23)
            a = clamp((1 - d) * 1.5 * (0.6 + 0.8 * n), 0, 1)
            col = mix((255, 250, 210), (255, 140, 40), clamp(d * 1.6, 0, 1))
            c.set(x, y, (col[0], col[1], col[2], int(255 * a)))
    return c


def shadow_texture():
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5) / 31.0
            a = clamp(1.0 - d, 0, 1)
            a = a ** 0.6
            c.set(x, y, (0, 0, 0, int(200 * a)))
    return c


def noise_texture(seed, size=64, base=(128, 128, 128), strength=0.3):
    c = Canvas(size, size, base)
    c.noise_overlay(size / 8.0, strength, seed=seed)
    return c


def cloud_texture():
    c = Canvas(128, 128, (255, 255, 255, 255))
    for y in range(128):
        for x in range(128):
            n = _periodic_noise(x, y, 128, 24, 71) * 0.6 + _periodic_noise(x, y, 128, 10, 72) * 0.4
            v = int(clamp((n - 0.35) * 2.2, 0, 1) * 255)
            c.set(x, y, (v, v, v, 255))
    return c


def scorch_texture():
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5) / 31.0
            n = fbm(x / 7.0, y / 7.0, 3, 91)
            a = clamp((1 - d) * 1.3 * (0.6 + 0.8 * n), 0, 1)
            c.set(x, y, (12, 10, 8, int(235 * a)))
    return c


def bib_texture():
    c = Canvas(64, 64, (96, 96, 100, 255))
    c.noise_overlay(8, 0.2, seed=3)
    c.frame(0, 0, 64, 64, (60, 60, 64, 255), 2)
    return c


def alpha_clip():
    c = Canvas(64, 64, (255, 255, 255, 255))
    for y in range(64):
        for x in range(64):
            d = min(x, y, 63 - x, 63 - y) / 12.0
            v = int(clamp(d, 0, 1) * 255)
            c.set(x, y, (v, v, v, 255))
    return c


def missing_texture():
    c = Canvas(32, 32, (255, 0, 255, 255))
    for y in range(32):
        for x in range(32):
            if (x // 8 + y // 8) % 2:
                c.set(x, y, (40, 40, 40, 255))
    return c


def wave_texture(size):
    c = Canvas(size, size)
    for y in range(size):
        for x in range(size):
            n = _periodic_noise(x, y, size, max(4, size / 6.0), 13)
            a = clamp((n - 0.45) * 3.0, 0, 1)
            c.set(x, y, (255, 255, 255, int(120 * a)))
    return c


def moon_texture():
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5) / 30.0
            a = clamp((1.0 - d) * 6.0, 0, 1)
            n = fbm(x / 8.0, y / 8.0, 2, 3)
            v = int(clamp(205 + n * 40, 0, 255))
            c.set(x, y, (v, v, int(v * 0.95), int(255 * a)))
    return c


def edge_texture():
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            a = clamp(x / 63.0, 0, 1)
            c.set(x, y, (255, 255, 255, int(255 * a)))
    return c


def bubbles_texture():
    c = Canvas(64, 64)
    rng = Rng(61)
    for _ in range(40):
        cx, cy, r = rng.randint(0, 63), rng.randint(0, 63), rng.randint(1, 3)
        c.circle(cx, cy, r, (255, 255, 255, 150), filled=True)
    return c


def laser_texture():
    c = Canvas(16, 64)
    for y in range(64):
        for x in range(16):
            d = abs(x - 7.5) / 7.5
            c.set(x, y, (180, 220, 255, int(255 * clamp(1 - d, 0, 1) ** 1.5)))
    return c


def generate(emit):
    for name, sheet in (("swgrass", grass_sheet()), ("swdirt", dirt_sheet()), ("swrock", rock_sheet()),
                        ("swsand", sand_sheet()), ("swcliff", cliff_sheet())):
        emit("Art/Terrain/%s.tga" % name, sheet.to_tga(alpha=False, rle=True))
    textures = {
        "swsky": sky_texture(), "swwater": water_texture(), "sp_smoke": smoke_texture(), "sp_flame": flame_texture(),
        "sp_shadow": shadow_texture(), "shadow": shadow_texture(), "TSNoiseUrb": noise_texture(4),
        "TSCloudMed": cloud_texture(), "cloudmap": cloud_texture(), "EXScorch01": scorch_texture(),
        "TBBib": bib_texture(), "alphaclip": alpha_clip(), "missing": missing_texture(),
        "TSMoonLarg": moon_texture(), "Noise0000": noise_texture(9), "TWAlphaEdge": edge_texture(),
        "WaterSurfaceBubbles": bubbles_texture(), "EXLaser": laser_texture(), "TBRedBib": bib_texture(),
        "wave1": wave_texture(64), "wave2": wave_texture(64), "wave256": wave_texture(128),
    }
    for name, canvas in textures.items():
        has_alpha = name not in ("Noise0000", "TBRedBib", "swsky", "swwater", "TSNoiseUrb", "TBBib", "TSCloudMed", "cloudmap", "alphaclip", "missing")
        emit("Art/Textures/%s.tga" % name, canvas.to_tga(alpha=has_alpha, rle=True))
