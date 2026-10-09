#!/usr/bin/env python3
"""Writes the fixture folder of direct_test.cpp: pattern files (byte i of a pattern file is
((i % 1000003) * 2654435761 mod 2^32) >> 16 & 255) in a Zero Hour / Generals folder layout.

    make_fixture.py <out dir> [--big-mb N]      # N: size of ZeroHour/TexturesZH.big (default 40)
"""
import argparse
import os

PERIOD = 1000003


def pattern():
    return bytes(((k * 2654435761) & 0xFFFFFFFF) >> 16 & 0xFF for k in range(PERIOD))


def write_pattern(path, size, p):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        full = (size // PERIOD)
        # write in ~16 MB groups of whole periods; the pattern restarts at offset 0 of each period
        group = p * 16
        left = size
        while left >= len(group):
            f.write(group)
            left -= len(group)
        while left >= PERIOD:
            f.write(p)
            left -= PERIOD
        f.write(p[:left])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--big-mb', type=float, default=40)
    ap.add_argument('--many', type=int, default=0, help='also write N small files under ZeroHour/Data/Many (mount time test)')
    a = ap.parse_args()
    p = pattern()
    zh = os.path.join(a.out, 'ZeroHour')
    write_pattern(os.path.join(zh, 'INIZH.big'), 3 * 1024 * 1024, p)
    write_pattern(os.path.join(zh, 'TexturesZH.big'), int(a.big_mb * 1024 * 1024), p)
    os.makedirs(os.path.join(zh, 'Data', 'INI'), exist_ok=True)
    with open(os.path.join(zh, 'Data', 'INI', 'GameData.ini'), 'wb') as f:
        f.write(b'hello direct\n')
    with open(os.path.join(zh, 'generalszh.exe'), 'wb') as f:
        f.write(b'MZ' + b'\0' * 1000)
    with open(os.path.join(zh, 'setup.exe'), 'wb') as f:
        f.write(b'MZ')
    os.makedirs(os.path.join(zh, 'Movies'), exist_ok=True)
    with open(os.path.join(zh, 'Movies', 'intro.bik'), 'wb') as f:
        f.write(b'BIK')
    write_pattern(os.path.join(a.out, 'Generals', 'INI.big'), 1024 * 1024, p)
    for i in range(a.many):
        d = os.path.join(zh, 'Data', 'Many', 'Dir%02d' % (i % 40))
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'File%05d.ini' % i), 'wb') as f:
            f.write(b'x' * 100)


main()
