#!/usr/bin/env python3
"""Generates a tiny synthetic game data set for exercising the WebAssembly engine.

This is TEST DATA written from scratch for this repository. It contains no EA content: the INI
files below only hold the handful of settings the engine needs to get through start-up, and
every value is a plain default chosen here. It is NOT a playable game.

    gen_synthetic_data.py <output dir> [--stage N]

creates   <output dir>/ZeroHour/   (INIZH.big, W3DZH.big plus a loose Data/ directory)
          <output dir>/Generals/   (INI.big, W3D.big)
which the launcher imports like a real install (point smoke.mjs --data at <output dir>).

The stages grow the data set one engine subsystem at a time; a higher stage includes all the
lower ones. The default is the highest stage defined, so run the lower ones to find where an
engine failure comes from.
"""

import argparse
import os
import struct
import sys

# ----------------------------------------------------------------------------------------------
# BIG archives (format read by StdBIGFileSystem::openArchiveFile)
# ----------------------------------------------------------------------------------------------


def make_big(files):
    """files: list of (path inside the archive, bytes). Returns the archive bytes."""
    names = [(p.replace('/', '\\').encode('ascii') + b'\0', d) for p, d in files]
    header_size = 0x10 + sum(8 + len(n) for n, _ in names)
    offset = header_size
    directory = b''
    body = b''
    for name, data in names:
        directory += struct.pack('>II', offset, len(data)) + name
        body += data
        offset += len(data)
    total = header_size + len(body)
    # "BIGF", archive size (little endian, unused by the reader), number of files and the
    # offset of the first file (big endian).
    return b'BIGF' + struct.pack('<I', total) + struct.pack('>II', len(files), header_size) + directory + body


# ----------------------------------------------------------------------------------------------
# Content, by stage
# ----------------------------------------------------------------------------------------------

# Files that must exist for the engine to load a block of INI files but whose content does not
# matter for what is tested: empty INI files (comments only).
EMPTY_INI = [
    'Data\\INI\\Default\\GameData.ini',
]

# Stage 1: the engine can open the archives and load GameData.ini.
GAME_DATA = """\
; Synthetic test data. Not EA content.
GameData
  Windowed = Yes
  XResolution = 800
  YResolution = 600
  UseTrees = No
  UseFPSLimit = No
End
"""

STAGES = {
    1: {
        'inizh': [('Data\\INI\\GameData.ini', GAME_DATA)] + [(p, '; empty\n') for p in EMPTY_INI],
        'ini': [],
    },
}


def build(stage):
    inizh, ini = [], []
    for s in sorted(STAGES):
        if s > stage:
            break
        inizh += [(p, c.encode('latin-1')) for p, c in STAGES[s]['inizh']]
        ini += [(p, c.encode('latin-1')) for p, c in STAGES[s]['ini']]
    return inizh, ini


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('--stage', type=int, default=max(STAGES))
    ap.add_argument('--extra-empty', help='text file with more INI paths (one per line) to write as empty files; used while finding what the engine asks for')
    ap.add_argument('--loose', action='store_true', help='also write the Zero Hour INI files as loose files in mixed case (tests the plain file system)')
    args = ap.parse_args()

    inizh, ini = build(args.stage)
    if args.extra_empty:
        have = set(p.lower() for p, _ in inizh)
        for line in open(args.extra_empty):
            p = line.strip()
            if p and p.lower() not in have:
                inizh.append((p, b'; empty\n'))
                have.add(p.lower())
    zh = os.path.join(args.out, 'ZeroHour')
    gen = os.path.join(args.out, 'Generals')
    os.makedirs(zh, exist_ok=True)
    os.makedirs(gen, exist_ok=True)
    # The launcher identifies the folders by these archive names; the rest may be small.
    open(os.path.join(zh, 'INIZH.big'), 'wb').write(make_big(inizh or [('Data\\INI\\Placeholder.ini', b'')]))
    open(os.path.join(zh, 'W3DZH.big'), 'wb').write(make_big([('Art\\W3D\\placeholder.txt', b'')]))
    open(os.path.join(gen, 'INI.big'), 'wb').write(make_big(ini or [('Data\\INI\\Placeholder.ini', b'')]))
    open(os.path.join(gen, 'W3D.big'), 'wb').write(make_big([('Art\\W3D\\placeholder.txt', b'')]))
    if args.loose:
        for path, data in inizh:
            full = os.path.join(zh, *path.split('\\'))
            os.makedirs(os.path.dirname(full), exist_ok=True)
            open(full, 'wb').write(data)
    print('stage %d written to %s' % (args.stage, args.out))


if __name__ == '__main__':
    sys.exit(main())
