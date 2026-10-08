"""A minimal RGBA canvas with the drawing primitives the procedural art needs.

Pure Python (no Pillow/numpy) so the pack builds anywhere Python 3 runs.
Pixels are stored as a flat ``bytearray`` of RGBA bytes, top row first.
Colours are ``(r, g, b)`` or ``(r, g, b, a)`` tuples of 0..255 ints.
"""

import math

from .tga import write_tga
from .util import fbm, value_noise, hash_noise_2d, clamp


def _rgba(color):
    if len(color) == 3:
        return (color[0], color[1], color[2], 255)
    return tuple(color)


def mix(c1, c2, t):
    """Linear blend of two colours (RGBA tuples or RGB tuples), t in 0..1."""
    t = clamp(t, 0.0, 1.0)
    a, b = _rgba(c1), _rgba(c2)
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(4))


def shade(color, factor):
    """Multiply the RGB part of a colour by ``factor`` (clamped)."""
    c = _rgba(color)
    return (int(clamp(c[0] * factor, 0, 255)), int(clamp(c[1] * factor, 0, 255)),
            int(clamp(c[2] * factor, 0, 255)), c[3])


class Canvas:
    """An RGBA image you can draw into and save as TGA."""

    def __init__(self, width, height, fill=(0, 0, 0, 0)):
        self.width = width
        self.height = height
        self.data = bytearray(bytes(_rgba(fill)) * (width * height))

    # ---- pixels -------------------------------------------------------------
    def get(self, x, y):
        i = (y * self.width + x) * 4
        return tuple(self.data[i:i + 4])

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            i = (y * self.width + x) * 4
            self.data[i:i + 4] = bytes(_rgba(color))

    def blend(self, x, y, color):
        """Alpha-composite ``color`` over the pixel at (x, y)."""
        if not (0 <= x < self.width and 0 <= y < self.height):
            return
        r, g, b, a = _rgba(color)
        if a >= 255:
            self.set(x, y, (r, g, b, 255))
            return
        if a <= 0:
            return
        i = (y * self.width + x) * 4
        dr, dg, db, da = self.data[i:i + 4]
        sa = a / 255.0
        out_a = sa + (da / 255.0) * (1.0 - sa)
        if out_a <= 0:
            return
        k = (da / 255.0) * (1.0 - sa)
        self.data[i] = int(round((r * sa + dr * k) / out_a))
        self.data[i + 1] = int(round((g * sa + dg * k) / out_a))
        self.data[i + 2] = int(round((b * sa + db * k) / out_a))
        self.data[i + 3] = int(round(out_a * 255))

    # ---- filling ------------------------------------------------------------
    def fill(self, color):
        self.data[:] = bytes(_rgba(color)) * (self.width * self.height)

    def fill_func(self, func):
        """Set every pixel from ``func(x, y) -> colour`` (slow but simple)."""
        w = self.width
        data = self.data
        for y in range(self.height):
            for x in range(w):
                data[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(_rgba(func(x, y)))

    def rect(self, x0, y0, x1, y1, color, blend=False):
        """Filled rectangle covering [x0, x1) x [y0, y1)."""
        x0, y0 = max(0, x0), max(0, y0)
        x1, y1 = min(self.width, x1), min(self.height, y1)
        c = _rgba(color)
        if blend and c[3] < 255:
            for y in range(y0, y1):
                for x in range(x0, x1):
                    self.blend(x, y, c)
            return
        row = bytes(c) * max(0, x1 - x0)
        for y in range(y0, y1):
            i = (y * self.width + x0) * 4
            self.data[i:i + len(row)] = row

    def frame(self, x0, y0, x1, y1, color, thickness=1):
        """Rectangle outline (inclusive-exclusive like :meth:`rect`)."""
        t = thickness
        self.rect(x0, y0, x1, y0 + t, color)
        self.rect(x0, y1 - t, x1, y1, color)
        self.rect(x0, y0, x0 + t, y1, color)
        self.rect(x1 - t, y0, x1, y1, color)

    def bevel(self, x0, y0, x1, y1, light, dark, thickness=1):
        """A raised 3D-looking edge: light on top/left, dark on bottom/right."""
        for t in range(thickness):
            self.rect(x0 + t, y0 + t, x1 - t, y0 + t + 1, light)
            self.rect(x0 + t, y0 + t, x0 + t + 1, y1 - t, light)
            self.rect(x0 + t, y1 - t - 1, x1 - t, y1 - t, dark)
            self.rect(x1 - t - 1, y0 + t, x1 - t, y1 - t, dark)

    def gradient_v(self, x0, y0, x1, y1, top, bottom):
        h = max(1, y1 - y0 - 1)
        for y in range(y0, y1):
            self.rect(x0, y, x1, y + 1, mix(top, bottom, (y - y0) / h))

    def gradient_h(self, x0, y0, x1, y1, left, right):
        w = max(1, x1 - x0 - 1)
        for x in range(x0, x1):
            self.rect(x, y0, x + 1, y1, mix(left, right, (x - x0) / w))

    # ---- shapes -------------------------------------------------------------
    def line(self, x0, y0, x1, y1, color, thickness=1):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        r = thickness // 2
        while True:
            if thickness <= 1:
                self.blend(x0, y0, color)
            else:
                self.rect(x0 - r, y0 - r, x0 - r + thickness, y0 - r + thickness, color, blend=True)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def circle(self, cx, cy, radius, color, filled=True):
        r2 = radius * radius
        for y in range(int(cy - radius - 1), int(cy + radius + 2)):
            for x in range(int(cx - radius - 1), int(cx + radius + 2)):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if filled and d <= r2:
                    self.blend(x, y, color)
                elif not filled and abs(math.sqrt(d) - radius) <= 0.75:
                    self.blend(x, y, color)

    def polygon(self, points, color):
        """Scan-line filled polygon (even-odd rule)."""
        ys = [p[1] for p in points]
        for y in range(int(math.floor(min(ys))), int(math.ceil(max(ys))) + 1):
            xs = []
            n = len(points)
            for i in range(n):
                (xa, ya), (xb, yb) = points[i], points[(i + 1) % n]
                if (ya <= y + 0.5 < yb) or (yb <= y + 0.5 < ya):
                    xs.append(xa + (y + 0.5 - ya) * (xb - xa) / (yb - ya))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                for x in range(int(math.floor(xs[i] + 0.5)), int(math.floor(xs[i + 1] + 0.5))):
                    self.blend(x, y, color)

    # ---- compositing --------------------------------------------------------
    def paste(self, other, dx, dy, blend=True):
        """Copy ``other`` onto this canvas with its top-left at (dx, dy)."""
        for y in range(other.height):
            for x in range(other.width):
                px = other.get(x, y)
                if blend:
                    self.blend(dx + x, dy + y, px)
                else:
                    self.set(dx + x, dy + y, px)

    def scaled(self, new_w, new_h):
        """Nearest-neighbour resize (returns a new canvas)."""
        out = Canvas(new_w, new_h)
        for y in range(new_h):
            sy = min(self.height - 1, y * self.height // new_h)
            for x in range(new_w):
                out.set(x, y, self.get(min(self.width - 1, x * self.width // new_w), sy))
        return out

    # ---- procedural noise ---------------------------------------------------
    def noise_overlay(self, scale, strength, seed=0, octaves=3, tint=None, periodic=True):
        """Multiply brightness by fractal noise; tileable when ``periodic``."""
        w, h = self.width, self.height
        data = self.data
        for y in range(h):
            for x in range(w):
                if periodic:
                    n = _tileable_fbm(x / w, y / h, max(1, int(round(w / scale))), octaves, seed)
                else:
                    n = fbm(x / scale, y / scale, octaves, seed)
                f = 1.0 + (n - 0.5) * 2.0 * strength
                i = (y * w + x) * 4
                for k in range(3):
                    data[i + k] = int(clamp(data[i + k] * f, 0, 255))

    def speckle(self, count, color, rng, size=1):
        """Scatter ``count`` small dots of ``color``."""
        for _ in range(count):
            x = rng.randint(0, self.width - 1)
            y = rng.randint(0, self.height - 1)
            self.rect(x, y, x + size, y + size, color, blend=True)

    # ---- output -------------------------------------------------------------
    def to_tga(self, alpha=True, origin="top"):
        return write_tga(self.width, self.height, bytes(self.data), alpha=alpha, origin=origin)


def _tileable_fbm(u, v, cells, octaves, seed):
    """fbm over the unit square that wraps seamlessly on both axes."""
    total, amp, norm = 0.0, 1.0, 0.0
    period = cells
    for o in range(octaves):
        x, y = u * period, v * period
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

        def h(ix, iy):
            return hash_noise_2d(ix % period, iy % period, seed + o * 31)

        a, b = h(x0, y0), h(x0 + 1, y0)
        c, d = h(x0, y0 + 1), h(x0 + 1, y0 + 1)
        top = a + (b - a) * fx
        bottom = c + (d - c) * fx
        total += amp * (top + (bottom - top) * fy)
        norm += amp
        amp *= 0.5
        period *= 2
    return total / norm
