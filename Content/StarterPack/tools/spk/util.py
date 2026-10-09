"""Small shared helpers: deterministic random numbers, vectors, byte packing."""

import math
import struct


class Rng:
    """A tiny deterministic PRNG (xorshift64*), independent of Python's version.

    Python's ``random`` module is allowed to change its output between
    releases; the pack must be byte-identical everywhere, so we roll our own.
    """

    def __init__(self, seed=1):
        self.state = (seed * 0x9E3779B97F4A7C15 + 0x1234567) & 0xFFFFFFFFFFFFFFFF
        if self.state == 0:
            self.state = 0x2545F4914F6CDD1D
        for _ in range(4):
            self.next_u32()

    def next_u32(self):
        x = self.state
        x ^= x >> 12
        x ^= (x << 25) & 0xFFFFFFFFFFFFFFFF
        x ^= x >> 27
        self.state = x
        return ((x * 0x2545F4914F6CDD1D) & 0xFFFFFFFFFFFFFFFF) >> 32

    def random(self):
        """Uniform float in [0, 1)."""
        return self.next_u32() / 4294967296.0

    def uniform(self, lo, hi):
        return lo + (hi - lo) * self.random()

    def randint(self, lo, hi):
        """Uniform integer in [lo, hi] (inclusive)."""
        return lo + int(self.random() * (hi - lo + 1))

    def choice(self, seq):
        return seq[self.randint(0, len(seq) - 1)]

    def gauss(self):
        """Approximate standard normal (sum of uniforms)."""
        return sum(self.random() for _ in range(6)) - 3.0


def hash_noise_2d(ix, iy, seed=0):
    """Integer lattice hash in [0, 1), used for value noise."""
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n ^= n >> 16
    return (n & 0xFFFFFF) / 16777216.0


def smoothstep(t):
    return t * t * (3.0 - 2.0 * t)


def value_noise(x, y, seed=0):
    """Smooth 2D value noise in [0, 1)."""
    x0, y0 = math.floor(x), math.floor(y)
    fx, fy = smoothstep(x - x0), smoothstep(y - y0)
    a = hash_noise_2d(x0, y0, seed)
    b = hash_noise_2d(x0 + 1, y0, seed)
    c = hash_noise_2d(x0, y0 + 1, seed)
    d = hash_noise_2d(x0 + 1, y0 + 1, seed)
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy


def fbm(x, y, octaves=4, seed=0):
    """Fractal sum of value noise, normalised to [0, 1)."""
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise(x, y, seed + o * 17)
        norm += amp
        x *= 2.0
        y *= 2.0
        amp *= 0.5
    return total / norm


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def lerp(a, b, t):
    return a + (b - a) * t


def fixed_string(text, length):
    """NUL padded ASCII field as used by the W3D structs."""
    raw = text.encode("ascii")
    if len(raw) >= length:
        raise ValueError("name %r does not fit in %d bytes" % (text, length))
    return raw + b"\0" * (length - len(raw))


def pack_f32(*values):
    return struct.pack("<%df" % len(values), *values)
