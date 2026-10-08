"""WND window layout writer, plus a parser that mirrors the engine's reader.

The format is whatever ``GameWindowManager::winCreateFromScript``
(Core/GameEngine/Source/GameClient/GUI/GameWindowManagerScript.cpp) parses:

    FILE_VERSION = 2;
    STARTLAYOUTBLOCK
      LAYOUTINIT = <init function>;  LAYOUTUPDATE = ...;  LAYOUTSHUTDOWN = ...;
    ENDLAYOUTBLOCK
    WINDOW
      WINDOWTYPE = <USER|PUSHBUTTON|STATICTEXT|...>;
      SCREENRECT = UPPERLEFT: x y, BOTTOMRIGHT: x y, CREATIONRESOLUTION: w h;
      NAME = "File.wnd:Name";   STATUS = A+B;   STYLE = A+B;
      SYSTEMCALLBACK = "..."; INPUTCALLBACK = "..."; TOOLTIPCALLBACK = "..."; DRAWCALLBACK = "...";
      FONT = NAME: "Face", SIZE: 12, BOLD: 0;
      TEXT = "Label:InStringTable";
      TEXTCOLOR = ENABLED: r g b a, ENABLEDBORDER: r g b a, DISABLED: ..., HILITE: ...;
      ENABLEDDRAWDATA = IMAGE: <name|NoImage>, COLOR: r g b a, BORDERCOLOR: r g b a, ... (9 times);
      <gadget specific data>
      CHILD
        WINDOW ... END
      ENDALLCHILDREN
    END

Child rectangles are in absolute screen coordinates of the creation resolution
(the parser subtracts the parent's position and scales to the display size).
"""

import re

MAX_DRAW_DATA = 9

KINDS = {"USER", "PUSHBUTTON", "RADIOBUTTON", "CHECKBOX", "VERTSLIDER", "HORZSLIDER", "SCROLLLISTBOX",
         "ENTRYFIELD", "STATICTEXT", "PROGRESSBAR", "COMBOBOX", "TABCONTROL", "TABPANE"}

# W3D draw callbacks / system callbacks default per kind are installed by the
# window manager itself, so a layout only names callbacks it wants to override.

STATUS_NAMES = ["ACTIVE", "TOGGLE", "DRAGABLE", "ENABLED", "HIDDEN", "ABOVE", "BELOW", "IMAGE", "TABSTOP",
                "NOINPUT", "NOFOCUS", "DESTROYED", "BORDER", "SMOOTH_TEXT", "ONE_LINE", "NO_FLUSH", "SEE_THRU",
                "RIGHT_CLICK", "WRAP_CENTERED", "CHECK_LIKE", "HOTKEY_TEXT", "USE_OVERLAY_STATES", "NOT_READY",
                "FLASHING", "ALWAYS_COLOR", "ON_MOUSE_DOWN"]
STYLE_NAMES = ["PUSHBUTTON", "RADIOBUTTON", "CHECKBOX", "VERTSLIDER", "HORZSLIDER", "SCROLLLISTBOX", "ENTRYFIELD",
               "STATICTEXT", "PROGRESSBAR", "USER", "MOUSETRACK", "ANIMATED", "TABSTOP", "TABCONTROL", "TABPANE",
               "COMBOBOX"]

TRANSPARENT = (0, 0, 0, 0)


class Look:
    """Colours/images for the three visual states of a window.

    Each state is a list of up to ``MAX_DRAW_DATA`` ``(image, color, border)``
    triples (image ``None`` becomes ``NoImage``). Index 0 is the plain state,
    index 1 the "selected" variant for buttons/check boxes.
    """

    def __init__(self, enabled=None, disabled=None, hilite=None):
        self.enabled = enabled or []
        self.disabled = disabled or []
        self.hilite = hilite or []

    @staticmethod
    def flat(color, border=TRANSPARENT, hilite=None, selected=None, disabled=None, image=None, hilite_image=None,
             selected_image=None):
        """One colour in all states; optional hilite/selected/disabled overrides (colour, border)."""
        e = [(image, color, border)]
        h = [(hilite_image or image, (hilite or (color, border))[0], (hilite or (color, border))[1])]
        d = [(image, (disabled or (color, border))[0], (disabled or (color, border))[1])]
        if selected:
            e.append((selected_image or image, selected[0], selected[1]))
            h.append((selected_image or image, selected[0], selected[1]))
            d.append((selected_image or image, selected[0], selected[1]))
        return Look(e, d, h)


class TextStyle:
    def __init__(self, enabled=((255, 255, 255, 255), (0, 0, 0, 255)),
                 disabled=((140, 140, 140, 255), (0, 0, 0, 255)),
                 hilite=((255, 235, 140, 255), (0, 0, 0, 255))):
        self.enabled, self.disabled, self.hilite = enabled, disabled, hilite


class Window:
    """One window or gadget and its children."""

    def __init__(self, kind, name, rect, status=("ENABLED",), text=None, font=("DejaVu Sans", 12, 0),
                 system=None, input=None, tooltip=None, draw=None, look=None, text_style=None,
                 tooltip_text=None, data=None, hidden=False, image=False, track=True):
        if kind not in KINDS:
            raise ValueError("unknown window type %r" % kind)
        self.kind = kind
        self.track = track
        self.name = name
        self.rect = tuple(rect)
        self.status = list(status)
        if hidden and "HIDDEN" not in self.status:
            self.status.append("HIDDEN")
        if image and "IMAGE" not in self.status:
            self.status.append("IMAGE")
        self.text = text
        self.font = font
        self.system, self.input, self.tooltip, self.draw = system, input, tooltip, draw
        self.look = look
        self.text_style = text_style
        self.tooltip_text = tooltip_text
        self.data = data or {}
        self.children = []
        # extra named draw data blocks, e.g. LISTBOXENABLEDUPBUTTONDRAWDATA
        self.extra_draw = {}

    def add(self, *children):
        self.children.extend(children)
        return self

    # Gadgets that react to the mouse hovering over them (highlight, tooltips) are mouse tracking.
    MOUSE_TRACKING = {"PUSHBUTTON", "RADIOBUTTON", "CHECKBOX", "VERTSLIDER", "HORZSLIDER", "SCROLLLISTBOX",
                      "ENTRYFIELD", "COMBOBOX"}

    def style_string(self):
        if self.kind in self.MOUSE_TRACKING and self.track:
            return self.kind + "+MOUSETRACK"
        return self.kind

    def walk(self):
        yield self
        for c in self.children:
            for w in c.walk():
                yield w


def _color(c):
    return "%d %d %d %d" % tuple(c)


def _draw_block(token, entries):
    entries = list(entries) + [(None, TRANSPARENT, TRANSPARENT)] * (MAX_DRAW_DATA - len(entries))
    parts = []
    for image, color, border in entries[:MAX_DRAW_DATA]:
        parts.append("IMAGE: %s, COLOR: %s, BORDERCOLOR: %s" % (image or "NoImage", _color(color), _color(border)))
    return "  %s = %s;\n" % (token, ",\n    ".join(parts))


def _gadget_lines(win):
    d = win.data
    k = win.kind
    out = []
    if k == "SCROLLLISTBOX":
        cols = d.get("columns", 1)
        s = "LENGTH: %d, AUTOSCROLL: %d, " % (d.get("length", 20), d.get("autoscroll", 0))
        if d.get("scroll_if_at_end"):
            s += "SCROLLIFATEND: 1, "
        s += "AUTOPURGE: %d, SCROLLBAR: %d, MULTISELECT: %d, COLUMNS: %d, " % (
            d.get("autopurge", 0), d.get("scrollbar", 1), d.get("multiselect", 0), cols)
        if cols > 1:
            widths = d.get("column_widths") or [100 // cols] * cols
            s += "".join("COLUMNWIDTH: %d, " % w for w in widths)
        s += "FORCESELECT: %d" % d.get("forceselect", 0)
        out.append("  LISTBOXDATA = %s;\n" % s)
    elif k == "COMBOBOX":
        out.append("  COMBOBOXDATA = ISEDITABLE: %d, MAXCHARS: %d, MAXDISPLAY: %d, ASCIIONLY: %d, LETTERSANDNUMBERS: %d;\n" % (
            d.get("editable", 0), d.get("maxchars", 32), d.get("maxdisplay", 6), d.get("asciionly", 0),
            d.get("lettersandnumbers", 0)))
    elif k in ("HORZSLIDER", "VERTSLIDER"):
        out.append("  SLIDERDATA = MINVALUE: %d, MAXVALUE: %d;\n" % (d.get("min", 0), d.get("max", 100)))
    elif k == "RADIOBUTTON":
        out.append("  RADIOBUTTONDATA = GROUP: %d;\n" % d.get("group", 0))
    elif k == "STATICTEXT":
        out.append("  STATICTEXTDATA = CENTERED: %d;\n" % d.get("centered", 0))
    elif k == "ENTRYFIELD":
        out.append("  TEXTENTRYDATA = MAXLEN: %d, SECRETTEXT: %d, NUMERICALONLY: %d, ALPHANUMERICALONLY: %d, ASCIIONLY: %d;\n" % (
            d.get("maxlen", 32), d.get("secret", 0), d.get("numeric", 0), d.get("alphanumeric", 0), d.get("ascii", 0)))
    return out


def _window_lines(win, resolution, layout_name, indent=0):
    rx, ry = resolution
    x0, y0, x1, y1 = win.rect
    lines = ["WINDOW\n",
             "  WINDOWTYPE = %s;\n" % win.kind,
             "  SCREENRECT = UPPERLEFT: %d %d,\n               BOTTOMRIGHT: %d %d,\n               CREATIONRESOLUTION: %d %d;\n" % (
                 x0, y0, x1, y1, rx, ry),
             '  NAME = "%s";\n' % (win.name if ":" in win.name else "%s:%s" % (layout_name, win.name)),
             "  STATUS = %s;\n" % ("+".join(win.status) if win.status else "NULL"),
             "  STYLE = %s;\n" % win.style_string(),
             '  SYSTEMCALLBACK = "%s";\n' % (win.system or "[None]"),
             '  INPUTCALLBACK = "%s";\n' % (win.input or "[None]"),
             '  TOOLTIPCALLBACK = "%s";\n' % (win.tooltip or "[None]"),
             '  DRAWCALLBACK = "%s";\n' % (win.draw or "[None]"),
             '  FONT = NAME: "%s", SIZE: %d, BOLD: %d;\n' % tuple(win.font),
             '  HEADERTEMPLATE = "[NONE]";\n']
    if win.text is not None:
        lines.append('  TEXT = "%s";\n' % win.text)
    if win.tooltip_text:
        lines.append('  TOOLTIPTEXT = "%s";\n' % win.tooltip_text)
    ts = win.text_style or TextStyle()
    lines.append("  TEXTCOLOR = ENABLED: %s, ENABLEDBORDER: %s,\n              DISABLED: %s, DISABLEDBORDER: %s,\n              HILITE: %s, HILITEBORDER: %s;\n" % (
        _color(ts.enabled[0]), _color(ts.enabled[1]), _color(ts.disabled[0]), _color(ts.disabled[1]),
        _color(ts.hilite[0]), _color(ts.hilite[1])))
    lines.extend(_gadget_lines(win))
    look = win.look or Look()
    lines.append(_draw_block("ENABLEDDRAWDATA", look.enabled))
    lines.append(_draw_block("DISABLEDDRAWDATA", look.disabled or look.enabled))
    lines.append(_draw_block("HILITEDRAWDATA", look.hilite or look.enabled))
    for token, entries in win.extra_draw.items():
        lines.append(_draw_block(token, entries))
    if win.children:
        lines.append("  CHILD\n")
        for child in win.children:
            lines.extend(_window_lines(child, resolution, layout_name))
        lines.append("  ENDALLCHILDREN\n")
    lines.append("END\n")
    return lines


def write_wnd(layout_name, windows, resolution=(800, 600), init=None, update=None, shutdown=None):
    """Serialise a layout. ``layout_name`` is the file name (used to qualify window names)."""
    out = ["FILE_VERSION = 2;\n",
           "STARTLAYOUTBLOCK\n",
           "  LAYOUTINIT = %s;\n" % (init or "[None]"),
           "  LAYOUTUPDATE = %s;\n" % (update or "[None]"),
           "  LAYOUTSHUTDOWN = %s;\n" % (shutdown or "[None]"),
           "ENDLAYOUTBLOCK\n"]
    for w in windows:
        out.extend(_window_lines(w, resolution, layout_name))
    return "".join(out).encode("ascii")


# ---- parser (mirrors winCreateFromScript closely enough to validate files) ---------
class ParsedWindow:
    def __init__(self):
        self.kind = None
        self.rect = None
        self.resolution = None
        self.name = None
        self.status = []
        self.callbacks = {}
        self.font = None
        self.text = None
        self.draw_blocks = {}
        self.children = []

    def walk(self):
        yield self
        for c in self.children:
            for w in c.walk():
                yield w


def _tokens(text):
    """Split like the engine: whitespace separated, ``;`` and ``=`` handled by the callers."""
    return re.findall(r'"[^"]*"|[^\s;=]+|;|=', text)


def parse_wnd(data):
    """Parse a WND file. Returns ``(layout_dict, [ParsedWindow])`` or raises ``ValueError``."""
    text = data.decode("ascii")
    m = re.match(r"FILE_VERSION\s*=\s*(\d+);", text)
    if not m:
        raise ValueError("missing FILE_VERSION")
    version = int(m.group(1))
    layout = {"version": version}
    pos = m.end()
    if version >= 2:
        m = re.compile(r"\s*STARTLAYOUTBLOCK(.*?)ENDLAYOUTBLOCK", re.S).match(text, pos)
        if not m:
            raise ValueError("missing layout block")
        for key, value in re.findall(r"(LAYOUT\w+)\s*=\s*([^;]*);", m.group(1)):
            layout[key] = value.strip()
        pos = m.end()
    stream = text[pos:]
    # statements: "KEY = value ;" with values possibly spanning lines; keywords without '=' (WINDOW, CHILD, END...)
    windows = []
    stack = []
    current = None
    i = 0
    n = len(stream)
    word = re.compile(r"\s*([A-Z_]+)")
    while True:
        m = word.match(stream, i)
        if not m:
            if stream[i:].strip():
                raise ValueError("garbage near %r" % stream[i:i + 40])
            break
        key = m.group(1)
        i = m.end()
        if key == "WINDOW":
            w = ParsedWindow()
            if stack:
                stack[-1][0].children.append(w)
            else:
                windows.append(w)
            current = w
            stack.append([w, False])
            continue
        if key == "CHILD":
            stack[-1][1] = True
            continue
        if key == "ENDALLCHILDREN":
            continue
        if key == "END":
            stack.pop()
            current = stack[-1][0] if stack else None
            continue
        # key = value;
        eq = re.compile(r"\s*=\s*").match(stream, i)
        if not eq:
            raise ValueError("expected '=' after %s" % key)
        i = eq.end()
        end = stream.index(";", i)
        value = " ".join(stream[i:end].split())
        i = end + 1
        if current is None:
            raise ValueError("%s outside a window" % key)
        if key == "WINDOWTYPE":
            current.kind = value
        elif key == "SCREENRECT":
            nums = [int(x) for x in re.findall(r"-?\d+", value)]
            if len(nums) != 6:
                raise ValueError("bad SCREENRECT %r" % value)
            current.rect = tuple(nums[:4])
            current.resolution = tuple(nums[4:])
        elif key == "NAME":
            current.name = value.strip('"')
        elif key == "STATUS":
            current.status = [s for s in value.split("+") if s != "NULL"]
            for s in current.status:
                if s not in STATUS_NAMES:
                    raise ValueError("unknown status %s" % s)
        elif key in ("SYSTEMCALLBACK", "INPUTCALLBACK", "TOOLTIPCALLBACK", "DRAWCALLBACK"):
            current.callbacks[key] = value.strip('"')
        elif key == "FONT":
            fm = re.match(r'NAME:\s*"([^"]*)",\s*SIZE:\s*(\d+),\s*BOLD:\s*(\d+)', value)
            if not fm:
                raise ValueError("bad FONT %r" % value)
            current.font = (fm.group(1), int(fm.group(2)), int(fm.group(3)))
        elif key == "TEXT":
            current.text = value.strip('"')
        elif key.endswith("DRAWDATA"):
            count = value.count("IMAGE:")
            if count != MAX_DRAW_DATA:
                raise ValueError("%s has %d entries, expected %d" % (key, count, MAX_DRAW_DATA))
            current.draw_blocks[key] = value
    if stack:
        raise ValueError("unterminated WINDOW block")
    for w in windows:
        for x in w.walk():
            if x.kind not in KINDS or x.rect is None or x.name is None:
                raise ValueError("incomplete window %r" % (x.name,))
    return layout, windows
