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
    'Data\\INI\\GameLOD.ini',
    'Data\\INI\\GameLODPresets.ini',
    'Data\\INI\\Default\\Water.ini',
    'Data\\INI\\Water.ini',
    'Data\\INI\\Default\\Weather.ini',
    'Data\\INI\\Weather.ini',
    'Data\\INI\\Default\\Science.ini',
    'Data\\INI\\Science.ini',
    'Data\\INI\\Default\\Multiplayer.ini',
    'Data\\INI\\Multiplayer.ini',
    'Data\\INI\\Default\\Terrain.ini',
    'Data\\INI\\Terrain.ini',
    'Data\\INI\\Default\\Roads.ini',
    'Data\\INI\\Roads.ini',
    'Data\\english\\Language.ini',
    'Data\\INI\\AudioSettings.ini',
    'Data\\INI\\Default\\Music.ini',
    'Data\\INI\\Music.ini',
    'Data\\INI\\Default\\SoundEffects.ini',
    'Data\\INI\\SoundEffects.ini',
    'Data\\INI\\Default\\Speech.ini',
    'Data\\INI\\Speech.ini',
    'Data\\INI\\Default\\Voice.ini',
    'Data\\INI\\Voice.ini',
    'Data\\INI\\MiscAudio.ini',
    'Data\\INI\\Rank.ini',
    'Data\\INI\\Default\\PlayerTemplate.ini',
    'Data\\INI\\PlayerTemplate.ini',
    'Data\\INI\\ParticleSystem.ini',
    'Data\\INI\\Default\\FXList.ini',
    'Data\\INI\\FXList.ini',
    'Data\\INI\\Weapon.ini',
    'Data\\INI\\Default\\ObjectCreationList.ini',
    'Data\\INI\\ObjectCreationList.ini',
    'Data\\INI\\Locomotor.ini',
    'Data\\INI\\Default\\SpecialPower.ini',
    'Data\\INI\\SpecialPower.ini',
    'Data\\INI\\DamageFX.ini',
    'Data\\INI\\Armor.ini',
    'Data\\INI\\Default\\Object.ini',
    'Data\\INI\\Object.ini',
    'Data\\INI\\Default\\Upgrade.ini',
    'Data\\INI\\Upgrade.ini',
    'Data\\INI\\DrawGroupInfo.ini',
    'Data\\INI\\Animation2D.ini',
    'Data\\INI\\Mouse.ini',
    'Data\\english\\HeaderTemplate.ini',
    'Data\\INI\\WindowTransitions.ini',
    'Data\\INI\\Default\\ShellMenuScheme.ini',
    'Data\\INI\\ShellMenuScheme.ini',
    'Data\\INI\\InGameUI.ini',
    'Data\\INI\\Default\\CommandButton.ini',
    'Data\\INI\\CommandButton.ini',
    'Data\\INI\\CommandSet.ini',
    'Data\\INI\\Default\\ControlBarScheme.ini',
    'Data\\INI\\ControlBarScheme.ini',
    'Data\\INI\\ChallengeMode.ini',
    'Data\\INI\\Default\\Video.ini',
    'Data\\INI\\Video.ini',
    'Data\\INI\\Campaign.ini',
    'Data\\INI\\Eva.ini',
    'Data\\INI\\Default\\AIData.ini',
    'Data\\INI\\AIData.ini',
    'Data\\INI\\Default\\Crate.ini',
    'Data\\INI\\Crate.ini',
    'Data\\english\\CommandMap.ini',
    'Data\\INI\\CommandMap.ini',
]

# Stage 1: the engine can open the archives and load GameData.ini.
GAME_DATA = """\
; Synthetic test data. Not EA content.
GameData
  Windowed = Yes
  XResolution = 800
  YResolution = 600
  UseTrees = No
  UseFPSLimit = Yes
  PlayIntro = No
  ShellMapOn = No
  FramesPerSecondLimit = 30
End
"""

WEATHER = """\
; Synthetic test data. Not EA content. Zero would divide by zero in SnowManager::updateIniSettings.
Weather
  SnowTexture = snow.tga
  SnowFrequencyScaleX = 0.05
  SnowFrequencyScaleY = 0.05
  SnowAmplitude = 5.0
  SnowPointSize = 1.0
  SnowMaxPointSize = 64.0
  SnowMinPointSize = 0.0
  SnowQuadSize = 0.5
  SnowBoxDimensions = 200.0
  SnowBoxDensity = 1.0
  SnowVelocity = 4.0
  SnowPointSprites = Yes
  SnowEnabled = No
End
"""


# A window layout in the engine's .wnd text format (without the IMAGE status a window is drawn as
# a coloured rectangle, with it only its image is drawn): a screen filling window and two coloured
# rectangles, so that the 2D renderer (Display -> W3D -> Direct3D 8 -> WebGL2) has something to draw.
def draw_data(r, g, b, a):
    one = 'IMAGE: NoImage, COLOR: %d %d %d %d, BORDERCOLOR: 255 255 255 255,' % (r, g, b, a)
    return ' '.join([one] * 9).rstrip(',')


def window(name, rect, color, children=''):
    return """\
WINDOW
  WINDOWTYPE = USER;
  SCREENRECT = UPPERLEFT: %d %d,
               BOTTOMRIGHT: %d %d,
               CREATIONRESOLUTION: 800 600;
  NAME = "%s";
  STATUS = ENABLED;
  STYLE = USER;
  SYSTEMCALLBACK = "[None]";
  INPUTCALLBACK = "[None]";
  TOOLTIPCALLBACK = "[None]";
  DRAWCALLBACK = "[None]";
  FONT = NAME: "Arial", SIZE: 12, BOLD: 0;
  HEADERTEMPLATE = "[None]";
  TOOLTIPDELAY = -1;
  ENABLEDDRAWDATA = %s;
  DISABLEDDRAWDATA = %s;
  HILITEDRAWDATA = %s;
%sEND
""" % (rect + (name, draw_data(*color), draw_data(*color), draw_data(*color), children))


def layout(root, init='[None]'):
    return """\
FILE_VERSION = 2;
STARTLAYOUTBLOCK
  LAYOUTINIT = %s;
  LAYOUTUPDATE = [None];
  LAYOUTSHUTDOWN = [None];
ENDLAYOUTBLOCK
""" % init + root


# The real MainMenuInit (it also lifts the "movie is playing" render block that the intro leaves
# behind) looks windows up by name, so this layout has the ones it dereferences without a check.
# MainMenuInit reverses this group; the engine does not check that it exists (a null group would
# run forever, as the null pointer is a valid address in WebAssembly).
TRANSITIONS = """\
; Synthetic test data. Not EA content.
WindowTransition FadeWholeScreen
  FireOnce = Yes
End
"""

MAIN_MENU = layout(window('MainMenu.wnd:MainMenuParent', (0, 0, 800, 600), (30, 50, 110, 255),
    '  CHILD\n' +
    window('MainMenu.wnd:MapBorder', (0, 0, 10, 10), (0, 0, 0, 0)) +
    window('MainMenu.wnd:MapBorder1', (0, 0, 10, 10), (0, 0, 0, 0)) +
    window('MainMenu.wnd:MapBorder2', (0, 0, 10, 10), (0, 0, 0, 0)) +
    window('MainMenu.wnd:MapBorder3', (0, 0, 10, 10), (0, 0, 0, 0)) +
    window('MainMenu.wnd:MapBorder4', (0, 0, 10, 10), (0, 0, 0, 0)) +
    window('MainMenu.wnd:Red', (100, 100, 400, 300), (200, 40, 40, 255)) +
    window('MainMenu.wnd:Yellow', (450, 350, 700, 520), (230, 200, 30, 255)) + '  ENDALLCHILDREN\n'), init='MainMenuInit')

BLANK_WINDOW = layout(window('BlankWindow.wnd:Root', (0, 0, 800, 600), (0, 0, 0, 255)))

STAGES = {
    1: {
        'inizh': [('Data\\INI\\GameData.ini', GAME_DATA)] + [(p, '; empty\n') for p in EMPTY_INI],
        'ini': [],
    },
    2: {
        'inizh': [('Data\\INI\\Weather.ini', WEATHER)],
        'ini': [],
    },
    3: {
        'inizh': [('Data\\INI\\WindowTransitions.ini', TRANSITIONS), ('Window\\Menus\\MainMenu.wnd', MAIN_MENU), ('Window\\Menus\\BlankWindow.wnd', BLANK_WINDOW)],
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
