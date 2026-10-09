"""In game user interface: the control bar and the windows that belong to it.

The control bar code looks windows up by name (ControlBar.cpp, ControlBarCommand.cpp, ControlBarObserver.cpp,
ControlBarCallback.cpp, InGameUI.cpp ...) and the scheme (ControlBarScheme.ini) moves some of them, so the
names below are fixed by the engine; positions follow the "Ironwood" scheme in data/Data/INI/ControlBarScheme.ini.
"""

from spk.wnd import Window, Look, TextStyle, TRANSPARENT, write_wnd

from .textdb import text
from .wnd_widgets import (INK, MUTED, AMBER, AMBER_DIM, PANEL, PANEL_LIGHT, STEEL, BUTTON, BUTTON_HI, TEXT_STYLE,
                          MUTED_STYLE, AMBER_STYLE, PASS_ALL, panel, label, static, button, entry, progress, font)

RES = (800, 600)
BAR_TOP = 440


def image_button(name, rect, hidden=False, enabled=True):
    """A button whose picture comes from code (command buttons, queue slots...): drawn by the image draw
    function, with the engine's overlay states (grey when disabled, clock for build progress)."""
    status = ["ENABLED", "IMAGE"] if enabled else ["IMAGE"]
    look = Look.flat(TRANSPARENT, TRANSPARENT)
    return Window("PUSHBUTTON", name, rect, status=status, look=look, hidden=hidden, text_style=TEXT_STYLE,
                  font=font(10, True))


def user(name, rect, hidden=False, draw=None, input=None, system=PASS_ALL, fill=TRANSPARENT, border=TRANSPARENT, image=False,
         status=("ENABLED",)):
    w = Window("USER", name, rect, status=status, system=system, input=input, draw=draw, hidden=hidden,
               look=Look.flat(fill, border), image=image)
    return w


def small_button(name, rect, label_text=None, hidden=False):
    return button(name, rect, label_text, size=9, bold=True, hidden=hidden)


def control_bar():
    cmd = user("CommandWindow", (172, 462, 560, 598), system=PASS_ALL)
    for i in range(18):
        col, row = i % 7, (i // 7) % 2
        x0, y0 = 176 + col * 56, 466 + row * 44
        cmd.add(image_button("ButtonCommand%02d" % (i + 1), (x0, y0, x0 + 52, y0 + 40), hidden=i >= 14))

    # The queue strip sits under the two rows of command buttons (the engine shows it next to the command buttons while
    # something is in production, ControlBar.cpp), so more units can be queued with the buttons above it.
    queue = user("ProductionQueueWindow", (172, 552, 560, 598), hidden=True)
    for i in range(9):
        x0 = 176 + i * 42
        queue.add(image_button("ButtonQueue%02d" % (i + 1), (x0, 556, x0 + 38, 594), enabled=False))

    under = user("UnderConstructionWindow", (172, 462, 560, 598), hidden=True)
    under.add(static("UnderConstructionDesc", (180, 470, 552, 540), None, size=12, centered=True))
    under.add(button("ButtonCancelConstruction", (330, 552, 402, 590), text("CONTROLBAR:CancelConstruction", "Cancel"), size=12))

    ocl = user("OCLTimerWindow", (172, 462, 560, 598), hidden=True)
    ocl.add(static("OCLTimerStaticText", (180, 470, 552, 500), None, size=12, centered=True))
    ocl.add(progress("OCLTimerProgressBar", (200, 510, 532, 530)))
    ocl.add(button("OCLTimerSellButton", (330, 552, 402, 590), text("CONTROLBAR:SellShort", "Sell"), size=12))

    beacon = user("BeaconWindow", (172, 462, 560, 598), hidden=True, input="BeaconWindowInput")
    beacon.add(label("StaticTextBeaconLabel", (180, 470, 552, 494), text("CONTROLBAR:BeaconLabel", "Beacon text"), size=11))
    beacon.add(entry("EditBeaconText", (180, 498, 460, 524), maxlen=30))
    beacon.add(button("ButtonClearBeaconText", (466, 498, 552, 524), text("CONTROLBAR:BeaconClear", "Clear"), size=10))
    beacon.add(button("ButtonDeleteBeacon", (180, 540, 300, 570), text("CONTROLBAR:BeaconDelete", "Remove"), size=10))

    observer_list = user("ObserverPlayerListWindow", (172, 462, 560, 598), hidden=True)
    for i in range(8):
        x0, y0 = 176 + (i % 2) * 192, 466 + (i // 2) * 30
        observer_list.add(small_button("ButtonPlayer%d" % i, (x0, y0, x0 + 24, y0 + 24), None, hidden=True))
        observer_list.add(static("StaticTextPlayer%d" % i, (x0 + 28, y0, x0 + 184, y0 + 24), None, size=11, hidden=True))
    observer_info = user("ObserverPlayerInfoWindow", (172, 462, 560, 598), hidden=True)
    observer_info.add(static("StaticTextPlayerName", (180, 466, 400, 490), None, size=14, bold=True))
    observer_info.add(user("WinFlag", (410, 466, 450, 506), image=True))
    observer_info.add(user("WinGeneralPortrait", (456, 466, 520, 530), image=True))
    for j, nm in enumerate(("NumberOfUnits", "NumberOfBuildings", "NumberOfUnitsKilled", "NumberOfUnitsLost")):
        observer_info.add(static("StaticText" + nm, (180, 496 + j * 24, 400, 518 + j * 24), None, size=11))
    observer_info.add(button("ButtonCancel", (460, 560, 552, 590), text("CONTROLBAR:ObserverBack", "Back"), size=11))

    # the right hand panel: what is selected
    upgrades = [user("UnitUpgrade%d" % (i + 1), (700 + i * 18, 450, 716 + i * 18, 466), image=True) for i in range(5)]
    right = user("RightHUD", (574, 466, 796, 598), draw="W3DRightHUDDraw", image=True, fill=(10, 16, 26, 220), border=AMBER_DIM)
    unit_selected = user("WinUnitSelected", (578, 470, 792, 594))
    unit_selected.add(user("CameoWindow", (582, 474, 692, 560), image=True),
                      user("CameoMovieWindow", (582, 474, 692, 560), draw="W3DCameoMovieDraw", hidden=True),
                      *upgrades)
    right.add(unit_selected)

    top_strip = [
        static("MoneyDisplay", (172, 442, 272, 458), None, size=12, bold=True, style=AMBER_STYLE),
        user("PowerWindow", (280, 444, 470, 456), draw="W3DPowerDraw", system=None),
        small_button("ButtonIdleWorker", (478, 442, 500, 458), None),
        small_button("ButtonOptions", (504, 442, 526, 458), text("CONTROLBAR:OptionsShort", "Menu")),
        small_button("ButtonPlaceBeacon", (530, 442, 552, 458), None, hidden=True),
        small_button("PopupCommunicator", (556, 442, 578, 458), None, hidden=True),
        small_button("ButtonGeneral", (772, 442, 794, 458), None, hidden=True),
        small_button("ButtonLarge", (160, 442, 172, 456), None),
        user("GeneralsExp", (600, 442, 770, 458), system=None),
        user("ExpBarForeground", (600, 456, 770, 462), image=True, hidden=True),
    ]
    parent = Window("USER", "ControlBarParent", (0, BAR_TOP, 800, 600), status=("ENABLED",), system="ControlBarSystem",
                    input="ControlBarInput", draw="W3DCommandBarBackgroundDraw", look=Look.flat(TRANSPARENT))
    parent.add(
        user("BackgroundMarker", (0, BAR_TOP, 4, BAR_TOP + 4), status=("ENABLED", "NOINPUT")),
        # the attack glow frame is listed first: the engine hit-tests the children in reverse file order, and a
        # window that takes no input above the radar would swallow every radar click
        user("WinUAttack", (4, 446, 158, 596), status=("ENABLED", "NOINPUT"), image=True),
        user("LeftHUD", (6, 452, 156, 596), draw="W3DLeftHUDDraw", input="LeftHUDInput", system=None, fill=(0, 0, 0, 255),
             border=AMBER_DIM),
        *top_strip,
        cmd, queue, under, ocl, beacon, observer_list, observer_info, right,
    )
    return write_wnd("ControlBar.wnd", [parent], RES)


def gen_exp_points():
    parent = user("GenExpParent", (150, 90, 650, 440), fill=PANEL_LIGHT, border=AMBER, system="GeneralsExpPointsSystem",
                  input="GeneralsExpPointsInput", image=True)
    parent.add(
        static("StaticTextTitle", (160, 98, 640, 128), text("CONTROLBAR:ExperienceTitle", "Experience"), size=18, bold=True,
               style=AMBER_STYLE, centered=True),
        static("StaticTextLevel", (160, 134, 640, 160), None, size=13, centered=True),
        static("StaticTextRankPointsAvailable", (160, 164, 640, 190), None, size=12, centered=True),
        progress("ProgressBarExperience", (180, 196, 620, 214)),
        button("ButtonExit", (290, 396, 510, 430), text("CONTROLBAR:Close", "Close"), size=13),
    )
    for i in range(4):
        parent.add(image_button("ButtonRank1Number%d" % i, (170 + i * 60, 230, 220 + i * 60, 270), hidden=True))
        parent.add(image_button("ButtonRank8Number%d" % i, (170 + i * 60, 340, 220 + i * 60, 380), hidden=True))
    for i in range(15):
        parent.add(image_button("ButtonRank3Number%d" % i, (170 + (i % 8) * 56, 276 + (i // 8) * 48, 218 + (i % 8) * 56,
                                                           316 + (i // 8) * 48), hidden=True))
    return write_wnd("GeneralsExpPoints.wnd", [parent], RES)


def popup_description():
    parent = user("ControlBarPopupDescriptionParent", (172, 340, 560, 442), fill=(12, 18, 30, 240), border=AMBER, system=None)
    parent.add(
        static("StaticTextName", (180, 344, 460, 364), None, size=13, bold=True, style=AMBER_STYLE, hotkey=True),
        static("StaticTextCost", (464, 344, 556, 364), None, size=12, bold=True),
        static("StaticTextDescription", (180, 368, 552, 438), None, size=11),
    )
    return write_wnd("ControlBarPopupDescription.wnd", [parent], RES)


def generate(emit):
    emit("Window/ControlBar.wnd", control_bar())
    emit("Window/GeneralsExpPoints.wnd", gen_exp_points())
    emit("Window/ControlBarPopupDescription.wnd", popup_description())
