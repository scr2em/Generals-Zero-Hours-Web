"""Names and descriptions of the key bindings (``Data/INI/CommandMap.ini``), for the keyboard options screen.

Every ``CommandMap`` block names two string labels, ``GUI:Key_<NAME>`` and ``GUI:Key_<NAME>_Desc``; the options screen
lists the display names by category (``GUI:<CATEGORY>``) and shows the description of the chosen entry. The English
texts come from this table, with a rule for the families (team groups, saved views, release events).
"""

import os
import re

from .textdb import text

HERE = os.path.dirname(os.path.abspath(__file__))

NAMES = {
    "STOP": ("Stop", "Stop whatever the selected units are doing."),
    "SCATTER": ("Scatter", "Spread the selected units out around where they stand."),
    "SELECT_ALL": ("Select all units", "Select every unit you own."),
    "SELECT_MATCHING_UNITS": ("Select the same kind", "Select all units of the same kind that you can see."),
    "SELECT_NEXT_UNIT": ("Select next unit", "Select the next unit in the list."),
    "SELECT_PREV_UNIT": ("Select previous unit", "Select the previous unit in the list."),
    "SELECT_NEXT_IDLE_WORKER": ("Select idle worker", "Select the next worker that has nothing to do."),
    "VIEW_COMMAND_CENTER": ("Go to headquarters", "Move the camera to your headquarters."),
    "CAMERA_RESET": ("Reset the camera", "Return the camera to its default angle and zoom."),
    "BEGIN_CAMERA_ROTATE_LEFT": ("Rotate camera left", "Turn the camera to the left while the keys are held."),
    "BEGIN_CAMERA_ROTATE_RIGHT": ("Rotate camera right", "Turn the camera to the right while the keys are held."),
    "BEGIN_CAMERA_ZOOM_IN": ("Zoom in", "Bring the camera closer to the ground while the keys are held."),
    "BEGIN_CAMERA_ZOOM_OUT": ("Zoom out", "Move the camera away from the ground while the keys are held."),
    "TOGGLE_PAUSE": ("Pause", "Pause or continue the game."),
    "TAKE_SCREENSHOT": ("Take a screenshot", "Save a picture of the screen."),
    "OPTIONS": ("Options", "Open the options screen."),
    "CHAT_EVERYONE": ("Chat with everyone", "Open the chat line; Enter again sends the message to all players."),
    "CHAT_ALLIES": ("Chat with allies", "Open the chat line; Enter again sends the message to your allies."),
    "CHAT_PLAYERS": ("Chat with players", "Open the chat line for a message that only you see."),
    "DIPLOMACY": ("Show the player list", "Show or hide the list of players with their teams and status."),
    "PLACE_BEACON": ("Place a beacon", "Mark a place on the map for your allies."),
    "DELETE_BEACON": ("Remove a beacon", "Remove the beacon that you placed."),
    "TOGGLE_FAST_FORWARD_REPLAY": ("Fast forward", "Play a replay faster; press again for normal speed."),
}


def describe(name):
    if name in NAMES:
        return NAMES[name]
    m = re.fullmatch(r"(CREATE|SELECT|ADD|VIEW)_TEAM(\d)", name)
    if m:
        verb, n = m.groups()
        return {"CREATE": ("Create group %s" % n, "Remember the selected units as group %s." % n),
                "SELECT": ("Select group %s" % n, "Select the units of group %s." % n),
                "ADD": ("Add group %s to the selection" % n, "Add the units of group %s to what is selected." % n),
                "VIEW": ("Look at group %s" % n, "Move the camera to the units of group %s." % n)}[verb]
    m = re.fullmatch(r"(SAVE|VIEW)_VIEW(\d)", name)
    if m:
        verb, n = m.groups()
        return (("Save camera place %s" % n, "Remember where the camera is as place %s." % n) if verb == "SAVE" else
                ("Go to camera place %s" % n, "Move the camera to the remembered place %s." % n))
    m = re.fullmatch(r"END_(.*)", name)
    if m:
        base = describe("BEGIN_" + m.group(1))
        return (base[0] + " (release)", "Stops the movement started by the key above.")
    return (name.replace("_", " ").capitalize(), name.replace("_", " ").capitalize() + ".")


CATEGORIES = {"CONTROL": "Orders", "INFORMATION": "Camera and views", "INTERFACE": "Interface", "SELECTION": "Selecting units",
              "TAUNT": "Taunts", "TEAM": "Team", "MISC": "Other", "DEBUG": "Debug"}


def generate(emit):
    path = os.path.join(HERE, "..", "data", "Data", "INI", "CommandMap.ini")
    with open(path, encoding="latin-1") as f:
        names = re.findall(r"^CommandMap (\w+)", f.read(), re.M)
    for name in names:
        display, desc = describe(name)
        text("GUI:Key_%s" % name, display)
        text("GUI:Key_%s_Desc" % name, desc)
    for cat, english in CATEGORIES.items():
        # labels are looked up without regard to case: "GUI:TEAM" is the column heading "GUI:Team" of the setup screen
        text("GUI:Team" if cat == "TEAM" else "GUI:" + cat, english)
    text("KEYBOARD:Shift+", "Shift+")
    text("KEYBOARD:Ctrl+", "Ctrl+")
    text("KEYBOARD:Alt+", "Alt+")
