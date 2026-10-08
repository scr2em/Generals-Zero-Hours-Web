"""Odds and ends: the placeholder executable the engine reads, and the bundled free font.

``generalszh.exe``: the engine reads this file to compute the executable CRC (GlobalData::generateExeCRC).
It is never executed, so a short original text stands in.

Fonts: DejaVu Sans (Bitstream Vera license, which permits redistribution; the license text ships beside the
fonts). ``Language.ini`` names the family and lists the files in ``LocalFontFile`` so a platform layer can
register them.
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(os.path.dirname(HERE), "third_party", "dejavu")

PLACEHOLDER_EXE = (b"Starter pack placeholder. This file stands in for the game executable the engine "
                   b"fingerprints at start up; it is not a program.\r\n")


def generate(emit):
    emit("generalszh.exe", PLACEHOLDER_EXE)
    for name, dest in (("DejaVuSans.ttf", "DejaVuSans.ttf"), ("DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"),
                       ("LICENSE.txt", "DejaVu-LICENSE.txt")):
        with open(os.path.join(FONT_DIR, name), "rb") as f:
            emit("Data/Fonts/" + dest, f.read())
