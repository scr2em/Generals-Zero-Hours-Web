"""The string table of the pack: every label any INI or WND file uses is registered here.

``text("GUI:Skirmish", "Skirmish")`` returns the label and remembers the English text, so a label that is
used in a layout can never be missing from the table that ``zz_strings.py`` writes at the end of the build.
"""

_TABLE = {}


def text(label, english):
    if label in _TABLE and _TABLE[label] != english:
        raise ValueError("label %s defined twice with different text" % label)
    _TABLE[label] = english
    return label


def table():
    return dict(_TABLE)
