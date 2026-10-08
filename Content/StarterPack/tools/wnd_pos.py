#!/usr/bin/env python3
"""Print the centre of a window in a generated layout, in the 800x600 design resolution.

    python3 wnd_pos.py <pack dir> <layout.wnd> [WindowName ...]

Used to aim the mouse clicks of ``GeneralsMD/Code/Main/web/test/starter_flow.mjs`` at buttons (children are stored in
absolute screen coordinates). Without names, every button of the layout is listed.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from spk.wnd import parse_wnd  # noqa: E402


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    root = argv[1]
    if os.path.isdir(os.path.join(root, "StarterPack")):
        root = os.path.join(root, "StarterPack")
    path = None
    for dirpath, _d, names in os.walk(os.path.join(root, "window")):
        for n in names:
            if n.lower() == argv[2].lower():
                path = os.path.join(dirpath, n)
    if not path:
        print("layout not found")
        return 1
    with open(path, "rb") as f:
        _layout, windows = parse_wnd(f.read())
    wanted = {n.lower() for n in argv[3:]}
    for top in windows:
        for w in top.walk():
            short = w.name.split(":")[-1]
            if (wanted and short.lower() in wanted) or (not wanted and w.kind == "PUSHBUTTON"):
                x0, y0, x1, y1 = w.rect
                print("%-34s %-11s %4d,%-4d  rect %d,%d,%d,%d%s" % (short, w.kind, (x0 + x1) // 2, (y0 + y1) // 2, x0, y0, x1, y1,
                      "  hidden" if "HIDDEN" in w.status else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
