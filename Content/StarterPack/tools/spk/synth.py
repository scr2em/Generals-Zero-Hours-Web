"""Tiny software synthesiser for the pack's sound effects and music.

Everything produces lists of floats in [-1, 1] at a fixed sample rate
(``RATE``). Building blocks: oscillators, noise, ADSR-ish envelopes, one-pole
filters, delay, mixing and normalisation. Deterministic (uses :class:`Rng`).
"""

import math

from .util import Rng

RATE = 22050


def silence(seconds):
    return [0.0] * int(seconds * RATE)


def osc(freq, seconds, shape="sine", phase=0.0, freq_end=None):
    """An oscillator; ``freq_end`` makes it glide linearly from freq to freq_end."""
    n = int(seconds * RATE)
    out = []
    ph = phase
    for i in range(n):
        f = freq if freq_end is None else freq + (freq_end - freq) * (i / max(1, n - 1))
        ph += f / RATE
        frac = ph - math.floor(ph)
        if shape == "sine":
            v = math.sin(2 * math.pi * ph)
        elif shape == "square":
            v = 1.0 if frac < 0.5 else -1.0
        elif shape == "saw":
            v = 2.0 * frac - 1.0
        elif shape == "tri":
            v = 4.0 * abs(frac - 0.5) - 1.0
        else:
            raise ValueError(shape)
        out.append(v)
    return out


def noise(seconds, seed=1):
    rng = Rng(seed)
    return [rng.random() * 2.0 - 1.0 for _ in range(int(seconds * RATE))]


def envelope(samples, attack=0.005, decay=0.0, sustain=1.0, release=0.05, curve=1.0):
    """Apply an attack/decay/sustain/release envelope (times in seconds).

    ``curve`` > 1 makes the release/decay fall off faster (more percussive).
    """
    n = len(samples)
    a = int(attack * RATE)
    d = int(decay * RATE)
    r = int(release * RATE)
    out = []
    for i, s in enumerate(samples):
        if i < a:
            g = i / max(1, a)
        elif i < a + d:
            g = 1.0 - (1.0 - sustain) * ((i - a) / max(1, d))
        else:
            g = sustain
        if i >= n - r:
            g *= ((n - i) / max(1, r)) ** curve
        out.append(s * g)
    return out


def exp_decay(samples, tau):
    """Multiply by exp(-t/tau); tau in seconds."""
    k = 1.0 / (tau * RATE)
    return [s * math.exp(-i * k) for i, s in enumerate(samples)]


def lowpass(samples, cutoff):
    """One-pole low-pass filter."""
    a = 1.0 - math.exp(-2 * math.pi * cutoff / RATE)
    y = 0.0
    out = []
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out


def highpass(samples, cutoff):
    lp = lowpass(samples, cutoff)
    return [s - l for s, l in zip(samples, lp)]


def mix(*tracks, gains=None):
    """Sum tracks (padding the shorter ones with silence)."""
    n = max(len(t) for t in tracks)
    out = [0.0] * n
    for idx, t in enumerate(tracks):
        g = 1.0 if gains is None else gains[idx]
        for i, s in enumerate(t):
            out[i] += s * g
    return out


def concat(*tracks):
    out = []
    for t in tracks:
        out.extend(t)
    return out


def scale(samples, gain):
    return [s * gain for s in samples]


def offset(samples, seconds):
    """Delay a track by prepending silence."""
    return [0.0] * int(seconds * RATE) + list(samples)


def echo(samples, delay=0.12, feedback=0.35, taps=3):
    out = list(samples) + [0.0] * int(delay * RATE * taps)
    for tap in range(1, taps + 1):
        d = int(delay * RATE * tap)
        g = feedback ** tap
        for i, s in enumerate(samples):
            out[i + d] += s * g
    return out


def normalize(samples, peak=0.9):
    m = max((abs(s) for s in samples), default=0.0)
    if m <= 1e-9:
        return list(samples)
    k = peak / m
    return [s * k for s in samples]


def fade_edges(samples, ms=4):
    """Short fades so loops and clips never click."""
    n = int(ms * RATE / 1000)
    out = list(samples)
    for i in range(min(n, len(out))):
        g = i / n
        out[i] *= g
        out[-1 - i] *= g
    return out


def note_freq(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def pluck(freq, seconds, seed=1, brightness=0.5):
    """Karplus-Strong style plucked tone."""
    rng = Rng(seed)
    period = max(2, int(RATE / freq))
    buf = [rng.random() * 2 - 1 for _ in range(period)]
    out = []
    for i in range(int(seconds * RATE)):
        j = i % period
        nxt = buf[(j + 1) % period]
        v = (buf[j] + nxt) * 0.5 * (0.985 + 0.01 * brightness)
        buf[j] = v
        out.append(v)
    return out
