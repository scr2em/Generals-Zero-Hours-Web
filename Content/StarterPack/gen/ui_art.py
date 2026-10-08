"""User interface art: button cameos, command icons, small HUD icons and the full screen backdrops.

Everything is drawn procedurally. ``build()`` returns the packed atlas (``sp_ui.tga``), the stand-alone
backdrop textures and the MappedImage INI that tells the engine where every image sits.
"""

import math

from spk.image import Canvas, mix, shade
from spk.util import Rng

# Palette of the Ironwood Compact (steel blue and amber on dark slate).
SLATE = (24, 32, 44)
SLATE_LIGHT = (44, 58, 78)
STEEL = (92, 128, 176)
STEEL_LIGHT = (150, 184, 224)
AMBER = (232, 168, 48)
AMBER_DARK = (160, 108, 24)
MOSS = (84, 130, 72)
WHITE = (240, 244, 250)
RED = (210, 64, 56)

ATLAS_SIZE = 512


# --------------------------------------------------------------------------------------------------
# small drawing helpers
# --------------------------------------------------------------------------------------------------

def cameo_base(w=64, h=48, accent=STEEL):
    c = Canvas(w, h)
    c.gradient_v(0, 0, w, h, shade(accent, 0.55), shade(accent, 0.25))
    # faint diagonal sheen
    for i in range(0, w + h, 8):
        c.line(i, 0, i - h, h, (255, 255, 255, 10))
    c.frame(0, 0, w, h, AMBER_DARK)
    c.frame(1, 1, w - 1, h - 1, shade(AMBER, 0.75))
    return c


def outline(c, points, fill, edge=None):
    c.polygon(points, fill)
    edge = edge or shade(fill, 0.45)
    n = len(points)
    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        c.line(int(a[0]), int(a[1]), int(b[0]), int(b[1]), edge)


def box(c, x0, y0, x1, y1, fill, edge=None):
    c.rect(x0, y0, x1, y1, fill)
    c.rect(x0, y0, x1, y0 + 1, shade(fill, 1.3))
    c.rect(x0, y1 - 1, x1, y1, shade(fill, 0.55))
    c.rect(x1 - 1, y0, x1, y1, shade(fill, 0.7))


# --------------------------------------------------------------------------------------------------
# cameos (64x48)
# --------------------------------------------------------------------------------------------------

def cam_hq():
    c = cameo_base()
    box(c, 8, 28, 56, 42, STEEL)
    box(c, 22, 14, 42, 30, STEEL_LIGHT)
    c.circle(32, 14, 8, AMBER)
    c.circle(30, 12, 3, (255, 230, 150))
    box(c, 28, 33, 36, 42, SLATE)
    for x in (12, 18, 44, 50):
        c.rect(x, 32, x + 3, 35, (255, 220, 120))
    c.line(32, 6, 32, 1, WHITE)
    return c


def cam_power():
    c = cameo_base()
    box(c, 10, 24, 30, 42, STEEL)
    box(c, 34, 18, 54, 42, STEEL_LIGHT)
    c.rect(14, 14, 18, 24, SLATE_LIGHT)
    c.rect(42, 10, 46, 18, SLATE_LIGHT)
    bolt = [(31, 8), (23, 26), (30, 26), (26, 40), (40, 20), (32, 20), (37, 8)]
    outline(c, bolt, AMBER, AMBER_DARK)
    return c


def cam_barracks():
    c = cameo_base()
    outline(c, [(6, 26), (32, 12), (58, 26)], SLATE_LIGHT)
    box(c, 8, 26, 56, 42, STEEL)
    for x in range(12, 54, 10):
        c.rect(x, 30, x + 5, 36, SLATE)
    box(c, 28, 33, 36, 42, AMBER_DARK)
    c.line(32, 12, 32, 4, WHITE)
    outline(c, [(32, 4), (42, 6), (32, 9)], AMBER)
    return c


def cam_factory():
    c = cameo_base()
    box(c, 6, 22, 58, 42, STEEL)
    for i, x in enumerate(range(8, 52, 14)):
        outline(c, [(x, 22), (x + 14, 22), (x + 14, 12)], SLATE_LIGHT)
    box(c, 46, 8, 54, 22, STEEL_LIGHT)
    c.rect(24, 28, 40, 42, SLATE)
    c.gradient_v(25, 29, 39, 42, (70, 90, 120), (30, 40, 56))
    c.circle(50, 6, 3, (200, 200, 200, 120))
    return c


def person(c, cx, base, body, helmet, scale=1.0):
    s = scale
    c.circle(cx, base - 26 * s, 5 * s, (230, 190, 150))
    c.polygon([(cx - 6 * s, base - 28 * s), (cx + 6 * s, base - 28 * s), (cx + 5 * s, base - 31 * s),
               (cx - 5 * s, base - 31 * s)], helmet)
    c.circle(cx, base - 31 * s, 5 * s, helmet)
    c.polygon([(cx - 7 * s, base - 20 * s), (cx + 7 * s, base - 20 * s), (cx + 6 * s, base - 6 * s),
               (cx - 6 * s, base - 6 * s)], body)
    c.rect(int(cx - 6 * s), int(base - 6 * s), int(cx - 1 * s), int(base), shade(body, 0.6))
    c.rect(int(cx + 1 * s), int(base - 6 * s), int(cx + 6 * s), int(base), shade(body, 0.6))


def cam_worker():
    c = cameo_base()
    person(c, 28, 44, (200, 140, 40), AMBER, 1.0)
    c.line(36, 30, 46, 16, (190, 190, 200), 2)
    c.circle(47, 14, 4, (190, 190, 200), filled=False)
    c.rect(34, 28, 38, 32, (230, 190, 150))
    return c


def cam_rifleman():
    c = cameo_base()
    person(c, 26, 44, (70, 110, 150), (50, 80, 120), 1.0)
    c.line(30, 30, 52, 24, (40, 40, 48), 2)
    c.rect(48, 22, 55, 25, (40, 40, 48))
    c.rect(30, 28, 34, 33, (230, 190, 150))
    return c


def cam_rocketeer():
    c = cameo_base()
    person(c, 24, 44, (70, 110, 150), (50, 80, 120), 1.0)
    c.polygon([(28, 18), (58, 10), (60, 14), (30, 24)], (80, 90, 100))
    c.polygon([(56, 9), (62, 8), (62, 14), (58, 14)], AMBER)
    c.polygon([(26, 17), (32, 15), (34, 23), (28, 25)], (60, 70, 80))
    return c


def wheel(c, cx, cy, r):
    c.circle(cx, cy, r, (24, 24, 28))
    c.circle(cx, cy, r * 0.5, (150, 150, 160))


def cam_scout():
    c = cameo_base()
    outline(c, [(8, 32), (14, 24), (40, 24), (50, 30), (58, 32), (58, 38), (8, 38)], STEEL_LIGHT)
    outline(c, [(18, 25), (22, 17), (36, 17), (40, 25)], (60, 90, 130))
    c.rect(24, 12, 34, 15, (50, 50, 56))
    c.line(32, 14, 44, 10, (40, 40, 48), 2)
    wheel(c, 17, 39, 5)
    wheel(c, 47, 39, 5)
    return c


def cam_tank():
    c = cameo_base()
    outline(c, [(8, 34), (12, 28), (52, 28), (58, 34), (56, 40), (10, 40)], STEEL)
    c.rect(8, 36, 58, 43, (30, 34, 40))
    for x in range(12, 56, 7):
        c.circle(x, 39, 3, (80, 84, 92))
    outline(c, [(20, 28), (24, 19), (42, 19), (46, 28)], STEEL_LIGHT)
    c.rect(44, 21, 63, 24, (50, 50, 58))
    c.circle(33, 22, 2, AMBER)
    return c


# --------------------------------------------------------------------------------------------------
# command icons (48x48) and tiny HUD icons
# --------------------------------------------------------------------------------------------------

def icon_base(accent=SLATE_LIGHT):
    c = Canvas(48, 48)
    c.gradient_v(0, 0, 48, 48, shade(accent, 1.2), shade(accent, 0.6))
    c.frame(0, 0, 48, 48, AMBER_DARK)
    c.frame(1, 1, 47, 47, shade(AMBER, 0.7))
    return c


def ic_stop():
    c = icon_base()
    pts = [(16, 8), (32, 8), (40, 16), (40, 32), (32, 40), (16, 40), (8, 32), (8, 16)]
    outline(c, pts, RED, (120, 30, 26))
    c.rect(17, 21, 31, 27, WHITE)
    return c


def ic_attack_move():
    c = icon_base()
    c.circle(24, 24, 13, AMBER, filled=False)
    c.circle(24, 24, 6, AMBER, filled=False)
    for a, b, d, e in ((24, 4, 24, 13), (24, 35, 24, 44), (4, 24, 13, 24), (35, 24, 44, 24)):
        c.line(a, b, d, e, AMBER, 2)
    outline(c, [(30, 30), (44, 34), (36, 36), (34, 44)], WHITE)
    return c


def ic_guard():
    c = icon_base()
    outline(c, [(24, 6), (40, 12), (38, 30), (24, 43), (10, 30), (8, 12)], STEEL_LIGHT, (30, 50, 80))
    outline(c, [(24, 12), (34, 16), (33, 28), (24, 37), (15, 28), (14, 16)], STEEL)
    c.line(24, 14, 24, 34, WHITE, 2)
    c.line(17, 22, 31, 22, WHITE, 2)
    return c


def ic_sell():
    c = icon_base()
    c.circle(24, 24, 15, AMBER)
    c.circle(24, 24, 15, AMBER_DARK, filled=False)
    c.line(24, 12, 24, 36, SLATE, 3)
    c.line(18, 18, 28, 18, SLATE, 3)
    c.line(28, 18, 28, 22, SLATE, 3)
    c.line(18, 22, 28, 22, SLATE, 3)
    c.line(18, 22, 18, 30, SLATE, 3)
    c.line(18, 30, 28, 30, SLATE, 3)
    return c


def ic_rally():
    c = icon_base()
    c.line(16, 8, 16, 42, WHITE, 3)
    outline(c, [(18, 9), (40, 15), (18, 24)], AMBER, AMBER_DARK)
    c.circle(16, 42, 4, STEEL_LIGHT)
    return c


def ic_cancel():
    c = icon_base()
    c.line(12, 12, 36, 36, RED, 5)
    c.line(36, 12, 12, 36, RED, 5)
    return c


def ic_comm():
    c = icon_base()
    outline(c, [(8, 10), (40, 10), (40, 30), (24, 30), (14, 40), (16, 30), (8, 30)], WHITE, STEEL)
    for x in (16, 24, 32):
        c.circle(x, 20, 2, STEEL)
    return c


def small_icon(kind):
    c = Canvas(16, 16)
    if kind == "star_on":
        pts = [(8, 1), (10, 6), (15, 6), (11, 9), (13, 15), (8, 11), (3, 15), (5, 9), (1, 6), (6, 6)]
        outline(c, pts, AMBER, AMBER_DARK)
    elif kind == "star_off":
        pts = [(8, 1), (10, 6), (15, 6), (11, 9), (13, 15), (8, 11), (3, 15), (5, 9), (1, 6), (6, 6)]
        outline(c, pts, (70, 70, 80), (40, 40, 48))
    elif kind.startswith("chevron"):
        n = int(kind[-1])
        for i in range(n):
            y = 12 - i * 4
            c.line(2, y, 8, y - 4, AMBER, 2)
            c.line(8, y - 4, 14, y, AMBER, 2)
    elif kind == "ammo_full":
        c.rect(5, 2, 11, 14, AMBER)
        c.rect(5, 2, 11, 5, AMBER_DARK)
    elif kind == "ammo_empty":
        c.frame(5, 2, 11, 14, (90, 90, 100))
    elif kind == "pip_full":
        c.circle(8, 8, 4, WHITE)
    elif kind == "pip_empty":
        c.circle(8, 8, 4, (110, 110, 120), filled=False)
    return c


def side_emblem(size, observer=False):
    c = Canvas(size, size)
    r = size / 2 - 1
    c.circle(size / 2, size / 2, r, SLATE)
    c.circle(size / 2, size / 2, r, AMBER, filled=False)
    c.circle(size / 2, size / 2, r - 2, shade(AMBER, 0.6), filled=False)
    if observer:
        c.circle(size / 2, size / 2, size * 0.2, WHITE, filled=False)
        c.circle(size / 2, size / 2, size * 0.08, WHITE)
        c.line(int(size * 0.18), int(size / 2), int(size * 0.82), int(size / 2), WHITE)
        return c
    s = size / 64.0
    # a stylised tree ring: the Ironwood
    c.polygon([(32 * s, 10 * s), (46 * s, 36 * s), (18 * s, 36 * s)], MOSS)
    c.polygon([(32 * s, 20 * s), (50 * s, 48 * s), (14 * s, 48 * s)], shade(MOSS, 1.25))
    c.rect(int(29 * s), int(46 * s), int(35 * s), int(56 * s), (120, 84, 50))
    c.line(int(10 * s), int(52 * s), int(54 * s), int(52 * s), STEEL_LIGHT, max(1, int(2 * s)))
    return c


def unknown_map():
    c = Canvas(128, 128, SLATE)
    for y in range(0, 128, 16):
        c.line(0, y, 127, y, (255, 255, 255, 18))
        c.line(y, 0, y, 127, (255, 255, 255, 18))
    c.frame(0, 0, 128, 128, AMBER_DARK, 2)
    c.line(44, 44, 84, 84, WHITE, 4)
    c.line(84, 44, 44, 84, WHITE, 4)
    return c


# --------------------------------------------------------------------------------------------------
# backdrops
# --------------------------------------------------------------------------------------------------

def backdrop(width, height, seed, title=False):
    """A dark moody landscape: gradient sky, layered ridges, a few pine silhouettes."""
    rng = Rng(seed)
    c = Canvas(width, height)
    c.gradient_v(0, 0, width, height, (14, 22, 38), (54, 70, 96))
    # a low sun glow
    gx, gy = int(width * 0.72), int(height * 0.58)
    for r in range(int(height * 0.4), 0, -10):
        t = 1.0 - r / (height * 0.4)
        c.circle(gx, gy, r, (232, 168, 48, int(6 + 30 * t * t)))
    layers = [((38, 52, 70), 0.52, 26), ((28, 42, 56), 0.62, 34), ((20, 32, 42), 0.74, 44), ((12, 22, 28), 0.88, 56)]
    for index, (col, base, amp) in enumerate(layers):
        phase = rng.uniform(0, 100)
        pts = [(0, height)]
        for x in range(0, width + 8, 8):
            y = height * base - amp * (0.5 + 0.5 * math.sin(x / (width / (3.0 + index)) + phase)) \
                - amp * 0.3 * math.sin(x / 37.0 + phase * 2)
            pts.append((x, y))
        pts.append((width, height))
        c.polygon(pts, col)
    for _ in range(int(width / 18)):
        x = rng.randint(0, width)
        base = int(height * rng.uniform(0.78, 0.97))
        h = int(height * rng.uniform(0.06, 0.16))
        col = (10, 20 + rng.randint(0, 12), 22)
        for k in range(4):
            y0 = base - h + k * h // 4
            half = (k + 1) * h // 9
            c.polygon([(x, y0 - h // 5), (x - half, y0 + h // 4), (x + half, y0 + h // 4)], col)
        c.rect(x - 1, base, x + 2, base + h // 6, (30, 22, 16))
    # stars
    for _ in range(width // 6):
        x, y = rng.randint(0, width - 1), rng.randint(0, int(height * 0.4))
        c.blend(x, y, (255, 255, 255, rng.randint(40, 160)))
    return c


def bar_background(width=512, height=128):
    c = Canvas(width, height)
    c.gradient_v(0, 0, width, height, (34, 44, 60), (14, 20, 30))
    c.rect(0, 0, width, 3, AMBER_DARK)
    c.rect(0, 3, width, 5, shade(AMBER, 0.55))
    rng = Rng(11)
    for _ in range(300):
        c.blend(rng.randint(0, width - 1), rng.randint(6, height - 1), (255, 255, 255, rng.randint(4, 14)))
    return c


# --------------------------------------------------------------------------------------------------
# packing
# --------------------------------------------------------------------------------------------------

CAMEOS = [
    ("SP_HQ", cam_hq), ("SP_Power", cam_power), ("SP_Barracks", cam_barracks), ("SP_Factory", cam_factory),
    ("SP_Worker", cam_worker), ("SP_Rifleman", cam_rifleman), ("SP_Rocketeer", cam_rocketeer),
    ("SP_Scout", cam_scout), ("SP_Tank", cam_tank),
]
ICONS = [
    ("SP_Stop", ic_stop), ("SP_AttackMove", ic_attack_move), ("SP_Guard", ic_guard), ("SP_Sell", ic_sell),
    ("SP_Rally", ic_rally), ("SP_Cancel", ic_cancel), ("SP_Communicator", ic_comm),
]
SMALL = [
    ("BarButtonGenStarON", "star_on"), ("BarButtonGenStarOFF", "star_off"),
    ("SSChevron1L", "chevron1"), ("SSChevron2L", "chevron2"), ("SSChevron3L", "chevron3"),
    ("SCVeter1", "chevron1"), ("SCVeter2", "chevron2"), ("SCVeter3", "chevron3"),
    ("SCPAmmoFull", "ammo_full"), ("SCPAmmoEmpty", "ammo_empty"),
    ("SCPPipFull", "pip_full"), ("SCPPipEmpty", "pip_empty"),
]


def collect_images():
    items = []
    for name, fn in CAMEOS:
        items.append((name, fn()))
    for name, fn in ICONS:
        items.append((name, fn()))
    for name, kind in SMALL:
        items.append((name, small_icon(kind)))
    items.append(("SP_SideIronwood", side_emblem(64)))
    items.append(("SP_SideObserver", side_emblem(64, observer=True)))
    items.append(("UnknownMap", unknown_map()))
    return items


def pack(items, size=ATLAS_SIZE):
    """Shelf packer; returns the atlas canvas and {name: (x, y, w, h)}."""
    atlas = Canvas(size, size)
    placed = {}
    x = y = shelf = 0
    for name, img in sorted(items, key=lambda it: (-it[1].height, it[0])):
        w, h = img.width, img.height
        if x + w + 1 > size:
            x, y, shelf = 0, y + shelf + 1, 0
        if y + h + 1 > size:
            raise ValueError("atlas full")
        atlas.paste(img, x, y, blend=False)
        placed[name] = (x, y, w, h)
        x += w + 1
        shelf = max(shelf, h)
    return atlas, placed


def mapped_image_ini(entries, texture, tex_w, tex_h):
    out = []
    for name in sorted(entries):
        x, y, w, h = entries[name]
        out.append("MappedImage %s\n  Texture = %s\n  TextureWidth = %d\n  TextureHeight = %d\n"
                   "  Coords = Left:%d Top:%d Right:%d Bottom:%d\n  Status = NONE\nEnd\n"
                   % (name, texture, tex_w, tex_h, x, y, x + w, y + h))
    return "\n".join(out)


def build():
    """-> dict path -> bytes for the art textures plus the MappedImage INI files."""
    files = {}
    atlas, placed = pack(collect_images())
    files["Art/Textures/sp_ui.tga"] = atlas.to_tga(rle=True)
    ini = ["; Generated by gen/ui_art.py: where every starter content image sits in its texture.\n"]
    ini.append(mapped_image_ini(placed, "sp_ui.tga", ATLAS_SIZE, ATLAS_SIZE))

    full = {}
    bg = backdrop(512, 512, 3)
    files["Art/Textures/sp_menu_bg.tga"] = bg.to_tga(alpha=False, rle=True)
    full["SP_MenuBackdrop"] = ("sp_menu_bg.tga", 512, 512, 800, 600)
    load = backdrop(512, 512, 8)
    files["Art/Textures/sp_load_bg.tga"] = load.to_tga(alpha=False, rle=True)
    full["SP_LoadIronwood"] = ("sp_load_bg.tga", 512, 512, 800, 600)
    score = backdrop(512, 512, 21)
    files["Art/Textures/sp_score_bg.tga"] = score.to_tga(alpha=False, rle=True)
    full["SP_ScoreIronwood"] = ("sp_score_bg.tga", 512, 512, 800, 600)
    bar = bar_background()
    files["Art/Textures/sp_bar.tga"] = bar.to_tga(rle=True)
    full["SP_BarBackground"] = ("sp_bar.tga", 512, 128, 512, 128)
    for name in sorted(full):
        tex, tw, th, iw, ih = full[name]
        ini.append("MappedImage %s\n  Texture = %s\n  TextureWidth = %d\n  TextureHeight = %d\n"
                   "  Coords = Left:0 Top:0 Right:%d Bottom:%d\n  Status = NONE\nEnd\n" % (name, tex, tw, th, tw, th))
    files["Data/INI/MappedImages/HandCreated/StarterUI.ini"] = "\n".join(ini)
    return files, placed


def generate(emit):
    files, _ = build()
    for path in sorted(files):
        emit(path, files[path])
