"""Widget factories for the menus: one place that decides how buttons, labels, lists... look.

Everything is drawn with flat colours and thin borders (no images), which the engine's standard gadget
draw functions support directly. Colours: dark slate panels, amber accents.
"""

from spk.wnd import Window, Look, TextStyle, TRANSPARENT

FONT = "DejaVu Sans"

INK = (236, 240, 246, 255)
MUTED = (150, 160, 176, 255)
AMBER = (232, 168, 48, 255)
AMBER_DIM = (160, 112, 28, 255)
PANEL = (16, 24, 36, 232)
PANEL_LIGHT = (28, 40, 58, 240)
BUTTON = (40, 58, 84, 255)
BUTTON_HI = (64, 92, 132, 255)
BUTTON_DOWN = (232, 168, 48, 255)
BUTTON_OFF = (30, 36, 46, 255)
STEEL = (92, 128, 176, 255)
BLACK = (0, 0, 0, 255)
SHADOW = (0, 0, 0, 255)

TEXT_STYLE = TextStyle(enabled=(INK, SHADOW), disabled=(MUTED, SHADOW), hilite=((255, 232, 150, 255), SHADOW))
MUTED_STYLE = TextStyle(enabled=(MUTED, SHADOW), disabled=(MUTED, SHADOW), hilite=(MUTED, SHADOW))
AMBER_STYLE = TextStyle(enabled=(AMBER, SHADOW), disabled=(AMBER_DIM, SHADOW), hilite=(AMBER, SHADOW))

# system callbacks that forward gadget messages to the parent: a gadget reports to its owner (its direct
# parent), so every panel between a gadget and the menu root has to pass them on.
PASS_ALL = "PassMessagesToParentSystem"
PASS_SELECTED = "PassSelectedButtonsToParentSystem"


def font(size=12, bold=False):
    return (FONT, size, 1 if bold else 0)


def panel(name, rect, fill=PANEL, border=AMBER_DIM, system=PASS_ALL, hidden=False, draw=None, children=(), image=None,
          status=("ENABLED",)):
    w = Window("USER", name, rect, status=status, system=system, draw=draw, hidden=hidden,
               look=Look.flat(fill, border, image=image), image=bool(image))
    w.add(*children)
    return w


def label(name, rect, text, size=12, bold=False, centered=False, style=TEXT_STYLE, hidden=False, system=None):
    return Window("STATICTEXT", name, rect, status=("ENABLED", "NOINPUT") if False else ("ENABLED",), text=text,
                  font=font(size, bold), text_style=style, hidden=hidden, system=system,
                  data={"centered": 1 if centered else 0}, look=Look.flat(TRANSPARENT))


def static(name, rect, text=None, size=12, bold=False, centered=False, style=TEXT_STYLE, fill=TRANSPARENT,
           border=TRANSPARENT, hidden=False, hotkey=False):
    """A static text that code writes into (GadgetStaticTextSetText); optional boxed background. ``hotkey``: the text
    may carry a '&' in front of the hot key letter, which is then drawn underlined instead of as a character."""
    return Window("STATICTEXT", name, rect, status=("ENABLED", "HOTKEY_TEXT") if hotkey else ("ENABLED",), text=text, font=font(size, bold), text_style=style,
                  hidden=hidden, data={"centered": 1 if centered else 0}, look=Look.flat(fill, border))


def button(name, rect, text=None, size=13, bold=True, hidden=False, enabled=True, tooltip=None, image_status=False,
           system=None, draw=None):
    look = Look.flat(BUTTON, STEEL, hilite=(BUTTON_HI, AMBER), selected=(BUTTON_DOWN, AMBER), disabled=(BUTTON_OFF, (60, 66, 78, 255)))
    status = ["ENABLED"] if enabled else []
    return Window("PUSHBUTTON", name, rect, status=status, text=text, font=font(size, bold),
                  text_style=TextStyle(enabled=(INK, SHADOW), disabled=(MUTED, SHADOW), hilite=((255, 232, 150, 255), SHADOW)),
                  look=look, hidden=hidden, tooltip_text=tooltip, system=system, draw=draw, image=image_status)


# A colour the engine reads as "undefined": W3DGadgetCheckBoxDraw draws the cross of a check box unless the box colour
# is this one (GAME_COLOR_UNDEFINED = alpha 0, RGB white).
UNDEFINED = (255, 255, 255, 0)


CHECKBOX_HEIGHT = 28


def checkbox(name, rect, text=None, size=11):
    """Draw data of a check box (GadgetCheckBox.h): 0 = background, 1 = the box when unchecked, 2 = the box when checked.
    The engine (W3DCheckBox.cpp) puts the box a sixteenth of the width from the left edge, makes it a third of the height
    wide and draws the label one height from the left edge. For the box to clear the label the window has to be at
    most 16 * (2/3 height - 4) wide, so the rectangle given here only fixes the left edge and the middle line; the
    label may run past the right edge of the window (the background is transparent, nothing shows where it ends)."""
    x0, y0, x1, y1 = rect
    h = CHECKBOX_HEIGHT
    cy = (y0 + y1) // 2
    w = min(x1 - x0, 16 * (2 * h // 3 - 4))
    rect = (x0, cy - h // 2, x0 + w, cy - h // 2 + h)
    off = (None, TRANSPARENT, TRANSPARENT)
    look = Look([off, (None, UNDEFINED, STEEL), (None, AMBER, AMBER)],
                [off, (None, UNDEFINED, (60, 66, 78, 255)), (None, AMBER_DIM, AMBER_DIM)],
                [(None, (64, 92, 132, 60), TRANSPARENT), (None, UNDEFINED, AMBER), (None, AMBER, AMBER)])
    return Window("CHECKBOX", name, rect, status=("ENABLED",), text=text, font=font(size, False), text_style=TEXT_STYLE,
                  look=look)


def radio(name, rect, text, group=0, size=11):
    look = Look.flat(BUTTON, STEEL, hilite=(BUTTON_HI, AMBER), selected=(BUTTON_DOWN, AMBER))
    return Window("RADIOBUTTON", name, rect, status=("ENABLED",), text=text, font=font(size, False), text_style=TEXT_STYLE,
                  look=look, data={"group": group})


def entry(name, rect, maxlen=24, size=12):
    look = Look.flat((10, 16, 26, 255), STEEL, hilite=((18, 28, 44, 255), AMBER))
    return Window("ENTRYFIELD", name, rect, status=("ENABLED",), font=font(size, False), text_style=TEXT_STYLE,
                  look=look, data={"maxlen": maxlen})


def progress(name, rect, hidden=False):
    look = Look([(None, (10, 16, 26, 255), STEEL), (None, AMBER, TRANSPARENT)],
                [(None, (10, 16, 26, 255), STEEL), (None, AMBER_DIM, TRANSPARENT)],
                [(None, (10, 16, 26, 255), STEEL), (None, AMBER, TRANSPARENT)])
    return Window("PROGRESSBAR", name, rect, status=("ENABLED",), look=look, hidden=hidden)


def _list_parts(w, item_bg, item_sel):
    """Scroll bar parts for list boxes (and the list inside a combo box)."""
    flat = lambda c, b: [(None, c, b)]
    for state in ("ENABLED", "DISABLED", "HILITE"):
        w.extra_draw["LISTBOX%sUPBUTTONDRAWDATA" % state] = flat(BUTTON, STEEL)
        w.extra_draw["LISTBOX%sDOWNBUTTONDRAWDATA" % state] = flat(BUTTON, STEEL)
        w.extra_draw["LISTBOX%sSLIDERDRAWDATA" % state] = flat((8, 12, 20, 255), STEEL)
        w.extra_draw["SLIDERTHUMB%sDRAWDATA" % state] = [(None, BUTTON_HI, AMBER_DIM), (None, BUTTON_DOWN, AMBER)]


def listbox(name, rect, rows=8, columns=1, widths=None, size=11, scrollbar=True, forceselect=False, multiselect=False,
            autoscroll=False):
    look = Look([(None, (10, 16, 26, 240), STEEL), (None, (70, 100, 140, 255), AMBER)],
                [(None, (10, 16, 26, 240), STEEL), (None, (50, 58, 70, 255), MUTED)],
                [(None, (14, 22, 34, 240), AMBER_DIM), (None, (70, 100, 140, 255), AMBER)])
    w = Window("SCROLLLISTBOX", name, rect, status=("ENABLED",), font=font(size, False), text_style=TEXT_STYLE, look=look,
               data={"length": rows, "autoscroll": 1 if autoscroll else 0, "scrollbar": 1 if scrollbar else 0,
                     "columns": columns, "column_widths": widths, "forceselect": 1 if forceselect else 0,
                     "multiselect": 1 if multiselect else 0})
    _list_parts(w, None, None)
    return w


def combo(name, rect, display=6, size=11, editable=False):
    look = Look.flat((16, 26, 40, 255), STEEL, hilite=((26, 40, 60, 255), AMBER),
                     disabled=((30, 36, 46, 255), (60, 66, 78, 255)))
    w = Window("COMBOBOX", name, rect, status=("ENABLED",), font=font(size, False), text_style=TEXT_STYLE, look=look,
               data={"maxdisplay": display, "editable": 1 if editable else 0, "maxchars": 32})
    flat = lambda c, b: [(None, c, b)]
    w.extra_draw["COMBOBOXDROPDOWNBUTTONENABLEDDRAWDATA"] = flat(BUTTON, STEEL)
    w.extra_draw["COMBOBOXDROPDOWNBUTTONDISABLEDDRAWDATA"] = flat(BUTTON_OFF, (60, 66, 78, 255))
    w.extra_draw["COMBOBOXDROPDOWNBUTTONHILITEDRAWDATA"] = flat(BUTTON_HI, AMBER)
    w.extra_draw["COMBOBOXEDITBOXENABLEDDRAWDATA"] = flat((16, 26, 40, 255), STEEL)
    w.extra_draw["COMBOBOXEDITBOXDISABLEDDRAWDATA"] = flat((30, 36, 46, 255), (60, 66, 78, 255))
    w.extra_draw["COMBOBOXEDITBOXHILITEDRAWDATA"] = flat((26, 40, 60, 255), AMBER)
    sel = [(None, (10, 16, 26, 250), STEEL), (None, (70, 100, 140, 255), AMBER)]
    w.extra_draw["COMBOBOXLISTBOXENABLEDDRAWDATA"] = sel
    w.extra_draw["COMBOBOXLISTBOXDISABLEDDRAWDATA"] = sel
    w.extra_draw["COMBOBOXLISTBOXHILITEDRAWDATA"] = sel
    _list_parts(w, None, None)
    return w


def hslider(name, rect, lo, hi):
    look = Look.flat((10, 16, 26, 255), STEEL)
    w = Window("HORZSLIDER", name, rect, status=("ENABLED",), look=look, data={"min": lo, "max": hi})
    for state in ("ENABLED", "DISABLED", "HILITE"):
        w.extra_draw["SLIDERTHUMB%sDRAWDATA" % state] = [(None, BUTTON_HI, AMBER_DIM), (None, BUTTON_DOWN, AMBER)]
    return w
