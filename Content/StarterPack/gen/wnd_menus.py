"""The shell menus and pop-ups as WND files: main menu, message boxes, quit menu, skirmish setup,
map selection, load screen and score screen.

Window names follow what the menu callbacks of the engine look up (see REQUIREMENTS.md for the list).
All text goes through ``textdb`` so a label can never be missing from the string table.
"""

from spk.wnd import Window, Look, TextStyle, TRANSPARENT, write_wnd

from .textdb import text
from .wnd_widgets import (INK, MUTED, AMBER, AMBER_DIM, PANEL, PANEL_LIGHT, STEEL, BLACK, TEXT_STYLE, MUTED_STYLE,
                          AMBER_STYLE, PASS_ALL, PASS_SELECTED, panel, label, static, button, checkbox, radio, entry,
                          progress, listbox, combo, hslider, font)

RES = (800, 600)
MAX_SLOTS = 8


def full_screen_image(name, image, system=None, input=None, draw=None, children=(), status=("ENABLED",)):
    w = Window("USER", name, (0, 0, 800, 600), status=status, system=system, input=input, draw=draw,
               look=Look.flat((0, 0, 0, 255), TRANSPARENT, image=image), image=bool(image))
    w.add(*children)
    return w


def shade(name, rect, alpha=140):
    return panel(name, rect, fill=(0, 0, 0, alpha), border=TRANSPARENT)


# --------------------------------------------------------------------------------------------------
# Blank background of the shell
# --------------------------------------------------------------------------------------------------

def blank_window():
    root = Window("USER", "BlankWindow", (0, 0, 800, 600), status=("ENABLED", "NOINPUT"),
                  look=Look.flat((0, 0, 0, 255)))
    return write_wnd("BlankWindow.wnd", [root], RES)


# --------------------------------------------------------------------------------------------------
# Main menu
# --------------------------------------------------------------------------------------------------

def main_menu():
    hidden_panel = lambda n: panel(n, (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True)
    # The main menu code looks up every drop-down panel and the save / load shortcut buttons of the
    # campaigns by name; the ones the starter content does not use stay hidden and empty.
    hidden_buttons = [button("Button%s" % n, (0, 0, 2, 2), None, hidden=True) for n in
                      ("USARecentSave", "USALoadGame", "GLARecentSave", "GLALoadGame", "ChinaRecentSave", "ChinaLoadGame")]
    main_panel = panel("MapBorder2", (56, 196, 316, 396), fill=PANEL, border=AMBER, hidden=True, children=[
        button("ButtonSkirmish", (76, 218, 296, 262), text("GUI:MenuSkirmish", "Skirmish"), size=16),
        button("ButtonExit", (76, 330, 296, 374), text("GUI:MenuExit", "Exit"), size=16),
        label("LabelMenuHint", (76, 280, 296, 322), text("GUI:MenuHint", "One original faction, one map,\nplayed against the computer."),
              size=10, style=MUTED_STYLE, centered=True),
    ])
    parent = Window("USER", "MainMenuParent", (0, 0, 800, 600), status=("ENABLED",),
                    system="MainMenuSystem", input="MainMenuInput", draw="W3DShellMenuSchemeDraw",
                    look=Look.flat((0, 0, 0, 255)))
    parent.add(
        label("LabelTitle", (56, 70, 560, 130), text("GUI:GameTitle", "IRONWOOD"), size=40, bold=True, style=AMBER_STYLE),
        label("LabelSubtitle", (58, 130, 560, 160), text("GUI:GameSubtitle", "Free starter content for the web port"),
              size=14, style=TEXT_STYLE),
        main_panel,
        hidden_panel("MapBorder"), hidden_panel("MapBorder1"), hidden_panel("MapBorder3"), hidden_panel("MapBorder4"),
        *hidden_buttons,
        label("LabelNotice", (20, 566, 780, 592),
              text("GUI:MenuNotice", "An original placeholder game. Not Command & Conquer; free software, GPL-3.0-or-later."),
              size=10, style=MUTED_STYLE, centered=True),
        label("LabelVersion", (600, 8, 792, 28), text("GUI:MenuVersion", "Starter content"), size=10, style=MUTED_STYLE),
    )
    return write_wnd("MainMenu.wnd", [parent], RES, init="MainMenuInit", update="MainMenuUpdate", shutdown="MainMenuShutdown")


# --------------------------------------------------------------------------------------------------
# Message boxes (the code moves the buttons, shows the ones it needs and fills the texts)
# --------------------------------------------------------------------------------------------------

def message_box(layout, quit_logo=False):
    """The code shows the buttons it needs (Ok / Yes on the left, No / Cancel on the right), moves them and
    fills the title and the message."""
    ok = button("ButtonOk", (260, 350, 380, 388), text("GUI:Ok", "OK"), hidden=True)
    yes = button("ButtonYes", (260, 350, 380, 388), text("GUI:Yes", "Yes"), hidden=True)
    no = button("ButtonNo", (420, 350, 540, 388), text("GUI:No", "No"), hidden=True)
    cancel = button("ButtonCancel", (420, 350, 540, 388), text("GUI:Cancel", "Cancel"), hidden=True)
    box = panel("MessageBoxParent", (240, 190, 560, 410), fill=PANEL_LIGHT, border=AMBER, hidden=True, children=[
        static("StaticTextTitle", (250, 198, 550, 228), None, size=16, bold=True, style=AMBER_STYLE, centered=True),
        static("StaticTextMessage", (254, 236, 546, 340), None, size=12, centered=True),
        ok, yes, no, cancel,
    ])
    name = "QuitMessageBoxFull" if quit_logo else "MessageBoxFull"
    root = Window("USER", name, (0, 0, 800, 600), status=("ENABLED",),
                  system="QuitMessageBoxSystem" if quit_logo else "MessageBoxSystem",
                  look=Look.flat((0, 0, 0, 150)))
    root.add(box)
    return write_wnd(layout, [root], RES)


# --------------------------------------------------------------------------------------------------
# In game menus: quit menu (also the "options" of the control bar) and the no-save variant
# --------------------------------------------------------------------------------------------------

def quit_menu(layout, parent_name):
    buttons = [
        button("ButtonReturn", (300, 232, 500, 272), text("GUI:ReturnToGame", "Return to game"), size=14),
        button("ButtonRestart", (300, 284, 500, 324), text("GUI:RestartMission", "Restart"), size=14),
        button("ButtonExit", (300, 336, 500, 376), text("GUI:ExitMission", "Exit to menu"), size=14),
        # The code looks these up and (de)activates them; the starter content has no save games or options page.
        button("ButtonOptions", (0, 0, 2, 2), None, hidden=True),
    ]
    if layout == "QuitMenu.wnd":
        buttons.append(button("ButtonSaveLoad", (0, 0, 2, 2), None, hidden=True))
    root = panel(parent_name, (270, 190, 530, 400), fill=PANEL_LIGHT, border=AMBER, system="QuitMenuSystem", hidden=False,
                 children=[label("LabelQuitTitle", (280, 198, 520, 226), text("GUI:QuitMenuTitle", "Paused"), size=18, bold=True,
                                 style=AMBER_STYLE, centered=True)] + buttons)
    return write_wnd(layout, [root], RES)


# --------------------------------------------------------------------------------------------------
# Skirmish setup
# --------------------------------------------------------------------------------------------------

ROW_Y0 = 150
ROW_H = 34


def skirmish_options():
    children = []
    children.append(label("StaticTextTitle", (24, 14, 520, 52), text("GUI:SkirmishTitle", "Skirmish"), size=26, bold=True,
                          style=AMBER_STYLE))
    # column headings
    children.append(label("StaticTextPlayers", (24, 122, 190, 144), text("GUI:Players", "Player"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextFaction", (200, 122, 330, 144), text("GUI:Faction", "Faction"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextColor", (340, 122, 430, 144), text("GUI:Color", "Color"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextTeam", (440, 122, 520, 144), text("GUI:Team", "Team"), size=11, style=MUTED_STYLE))
    children.append(panel("RowsBackdrop", (16, 116, 530, ROW_Y0 + ROW_H * MAX_SLOTS + 6), fill=PANEL, border=AMBER_DIM))
    for i in range(MAX_SLOTS):
        y = ROW_Y0 + i * ROW_H
        if i == 0:
            children.append(entry("TextEntryPlayerName", (24, y, 190, y + 26), maxlen=20))
        else:
            children.append(combo("ComboBoxPlayer%d" % i, (24, y, 190, y + 26), display=5))
        children.append(combo("ComboBoxPlayerTemplate%d" % i, (200, y, 330, y + 26), display=5))
        children.append(combo("ComboBoxColor%d" % i, (340, y, 430, y + 26), display=8))
        children.append(combo("ComboBoxTeam%d" % i, (440, y, 520, y + 26), display=5))
    # the map
    children.append(label("StaticTextMapPreview", (548, 96, 784, 118), text("GUI:Map", "Map"), size=11, style=MUTED_STYLE))
    map_window = panel("MapWindow", (548, 120, 784, 304), fill=(10, 16, 26, 255), border=AMBER,
                       system=PASS_ALL, draw="W3DDrawMapPreview")
    for i in range(MAX_SLOTS):
        map_window.add(button("ButtonMapStartPosition%d" % i, (0, 0, 22, 22), text("GUI:StartPos%d" % i, str(i + 1)),
                              size=10, hidden=True))
    children.append(map_window)
    children.append(static("TextEntryMapDisplay", (548, 310, 784, 336), None, size=12, bold=True, centered=True,
                           fill=(10, 16, 26, 255), border=STEEL))
    children.append(button("ButtonSelectMap", (548, 344, 784, 380), text("GUI:SelectMap", "Choose map"), size=13))
    # game options
    children.append(label("StaticTextStartingCash", (548, 392, 700, 414), text("GUI:StartingCash", "Starting cash"), size=11,
                          style=MUTED_STYLE))
    children.append(combo("ComboBoxStartingCash", (548, 416, 784, 442), display=5))
    children.append(checkbox("CheckboxLimitSuperweapons", (548, 452, 784, 476), text("GUI:LimitSuperweapons", "Limit superweapons")))
    children.append(label("StaticTextGameSpeedLabel", (548, 484, 700, 506), text("GUI:GameSpeed", "Game speed limit (fps)"), size=11,
                          style=MUTED_STYLE))
    children.append(hslider("SliderGameSpeed", (548, 508, 740, 528), 15, 61))
    children.append(static("StaticTextGameSpeed", (746, 506, 784, 530), None, size=12, bold=True))
    # record of the player's games against the computer
    children.append(label("StaticTextWins", (24, 436, 120, 458), text("GUI:Wins", "Wins"), size=11, style=MUTED_STYLE))
    children.append(static("StaticTextWinsValue", (120, 434, 180, 458), None, size=12, bold=True))
    children.append(label("StaticTextLosses", (200, 436, 300, 458), text("GUI:Losses", "Losses"), size=11, style=MUTED_STYLE))
    children.append(static("StaticTextLossesValue", (300, 434, 360, 458), None, size=12, bold=True))
    children.append(label("StaticTextStreak", (24, 462, 120, 484), text("GUI:Streak", "Win streak"), size=11, style=MUTED_STYLE))
    children.append(static("StaticTextStreakValue", (120, 460, 180, 484), None, size=12, bold=True))
    children.append(label("StaticTextBestStreak", (200, 462, 300, 484), text("GUI:BestStreak", "Best streak"), size=11, style=MUTED_STYLE))
    children.append(static("StaticTextBestStreakValue", (300, 460, 360, 484), None, size=12, bold=True))
    children.append(button("ButtonReset", (380, 440, 520, 478), text("GUI:ResetStats", "Reset stats"), size=11))
    # buttons
    children.append(button("ButtonBack", (24, 548, 184, 584), text("GUI:Back", "Back"), size=14))
    children.append(button("ButtonStart", (616, 548, 784, 584), text("GUI:Start", "Start"), size=16))
    children.append(label("StaticTextNotice", (200, 556, 600, 580),
                          text("GUI:SkirmishNotice", "Original placeholder content: one faction, one map."),
                          size=10, style=MUTED_STYLE, centered=True))
    children.append(panel("SubParent", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True))
    parent = full_screen_image("SkirmishGameOptionsMenuParent", "SP_MenuBackdrop", system="SkirmishGameOptionsMenuSystem",
                               input="SkirmishGameOptionsMenuInput", children=children)
    return write_wnd("SkirmishGameOptionsMenu.wnd", [parent], RES, update="SkirmishGameOptionsMenuUpdate",
                     init="SkirmishGameOptionsMenuInit", shutdown="SkirmishGameOptionsMenuShutdown")


def skirmish_map_select():
    kids = []
    kids.append(label("StaticTextTitle", (130, 70, 500, 100), text("GUI:ChooseMap", "Choose a map"), size=20, bold=True, style=AMBER_STYLE))
    kids.append(radio("RadioButtonSystemMaps", (140, 104, 300, 128), text("GUI:SystemMaps", "Standard maps"), group=1))
    kids.append(radio("RadioButtonUserMaps", (310, 104, 490, 128), text("GUI:UserMaps", "My maps"), group=1))
    kids.append(listbox("ListboxMap", (140, 134, 490, 480), rows=14, columns=2, widths=[88, 12], size=12, forceselect=True))
    preview = panel("WinMapPreview", (510, 134, 690, 320), fill=(10, 16, 26, 255), border=AMBER, system=PASS_ALL,
                    draw="W3DDrawMapPreview")
    for i in range(MAX_SLOTS):
        preview.add(button("ButtonMapStartPosition%d" % i, (0, 0, 20, 20), text("GUI:StartPos%d" % i, str(i + 1)),
                           size=10, hidden=True))
    kids.append(preview)
    kids.append(button("ButtonBack", (140, 492, 300, 526), text("GUI:Back", "Back"), size=14))
    kids.append(button("ButtonOK", (330, 492, 490, 526), text("GUI:Ok", "OK"), size=14))
    parent = panel("SkrimishMapSelectMenuParent", (110, 56, 720, 540), fill=(12, 18, 30, 248), border=AMBER,
                   system="SkirmishMapSelectMenuSystem", children=kids)
    parent.input = "SkirmishMapSelectMenuInput"
    return write_wnd("SkirmishMapSelectMenu.wnd", [parent], RES, update="SkirmishMapSelectMenuUpdate",
                     init="SkirmishMapSelectMenuInit", shutdown="SkirmishMapSelectMenuShutdown")


# --------------------------------------------------------------------------------------------------
# Loading screen of a skirmish game
# --------------------------------------------------------------------------------------------------

def multiplayer_load_screen():
    kids = []
    kids.append(label("LabelLoading", (40, 28, 500, 64), text("GUI:Loading", "Loading"), size=24, bold=True, style=AMBER_STYLE))
    kids.append(panel("LocalGeneralPortrait", (40, 80, 168, 208), fill=(10, 16, 26, 220), border=AMBER, image="SP_SideIronwood"))
    kids.append(static("LocalGeneralName", (180, 84, 520, 112), None, size=18, bold=True))
    kids.append(static("LocalGeneralFeatures", (180, 118, 520, 208), None, size=12, style=MUTED_STYLE))
    preview = panel("WinMapPreview", (560, 80, 760, 240), fill=(10, 16, 26, 255), border=AMBER, system=PASS_ALL,
                    draw="W3DDrawMapPreview")
    for i in range(MAX_SLOTS):
        preview.add(button("ButtonMapStartPosition%d" % i, (0, 0, 20, 20), text("GUI:StartPos%d" % i, str(i + 1)),
                           size=10, hidden=True, enabled=False))
    kids.append(preview)
    for i in range(MAX_SLOTS):
        y = 280 + i * 30
        kids.append(static("StaticTextPlayer%d" % i, (40, y, 280, y + 24), None, size=13, bold=True))
        kids.append(static("StaticTextSide%d" % i, (290, y, 450, y + 24), None, size=12))
        kids.append(static("StaticTextTeam%d" % i, (460, y, 540, y + 24), None, size=12))
        kids.append(progress("ProgressLoad%d" % i, (550, y + 2, 760, y + 22)))
    parent = full_screen_image("MultiplayerLoadScreenParent", "SP_LoadIronwood", children=kids)
    return write_wnd("MultiplayerLoadScreen.wnd", [parent], RES)


def shell_load_screen():
    kids = [label("StaticTextLegal", (40, 540, 760, 570), text("GUI:LegalNotice", "Original placeholder game. Free software, GPL-3.0-or-later."),
                  size=10, style=MUTED_STYLE, centered=True),
            progress("ProgressLoad", (200, 500, 600, 520))]
    parent = full_screen_image("ShellGameLoadScreenParent", "SP_LoadIronwood", children=kids)
    return write_wnd("ShellGameLoadScreen.wnd", [parent], RES)


# --------------------------------------------------------------------------------------------------
# Score screen
# --------------------------------------------------------------------------------------------------

SCORE_COLUMNS = [
    ("StaticTextPlayer", "GUI:ScorePlayer", "Player", 130),
    ("StaticTextUnitsBuilt", "GUI:ScoreUnitsBuilt", "Units built", 70),
    ("StaticTextUnitsLost", "GUI:ScoreUnitsLost", "Units lost", 70),
    ("StaticTextUnitsDestroyed", "GUI:ScoreUnitsKilled", "Units killed", 70),
    ("StaticTextBuildingsBuilt", "GUI:ScoreBuildingsBuilt", "Buildings built", 76),
    ("StaticTextBuildingsLost", "GUI:ScoreBuildingsLost", "Buildings lost", 76),
    ("StaticTextBuildingsDestroyed", "GUI:ScoreBuildingsKilled", "Buildings destroyed", 82),
    ("StaticTextResources", "GUI:ScoreResources", "Cash earned", 76),
]


def score_screen():
    kids = []
    kids.append(label("StaticTextTitle", (40, 24, 760, 66), text("GUI:ScoreTitle", "Battle report"), size=28, bold=True,
                      style=AMBER_STYLE))
    x = 30
    xs = []
    for token, lbl, eng, width in SCORE_COLUMNS:
        xs.append((token, x, width))
        kids.append(label("Heading" + token[10:], (x, 84, x + width, 106), text(lbl, eng), size=9, style=MUTED_STYLE, centered=token != "StaticTextPlayer"))
        x += width + 4
    kids.append(panel("RowsBackdrop", (22, 78, 780, 108 + 8 * 34 + 6), fill=PANEL, border=AMBER_DIM))
    for i in range(MAX_SLOTS):
        y = 112 + i * 34
        for token, x0, width in xs:
            kids.append(static("%s%d" % (token, i), (x0, y, x0 + width, y + 26), None, size=12, bold=token == "StaticTextPlayer",
                               centered=token != "StaticTextPlayer", hidden=True))
        kids.append(static("StaticTextObserver%d" % i, (xs[0][1], y, xs[0][1] + 130, y + 26), None, size=12, hidden=True))
        kids.append(static("StaticTextScore%d" % i, (700, y, 780, y + 26), None, size=12, hidden=True))
        kids.append(static("GameWindowWinner%d" % i, (740, y, 776, y + 26), text("GUI:ScoreWinner", "Winner"),
                           size=9, style=AMBER_STYLE, hidden=True))
    # parts of the full score screen the skirmish does not use (the code looks them up and hides them)
    kids.append(listbox("ListboxWarschoolAdvice", (30, 410, 560, 500), rows=5, size=11))
    kids.append(label("StaticTextWarSchool", (30, 386, 560, 408), text("GUI:ScoreAdvice", "Tips"), size=11, style=MUTED_STYLE))
    kids.append(entry("TextEntryChat", (30, 520, 400, 546)))
    kids.append(button("ButtonEmote", (410, 520, 500, 546), None, hidden=True))
    kids.append(panel("ChatBoxBorder", (28, 516, 504, 550), fill=TRANSPARENT, border=TRANSPARENT, hidden=True))
    kids.append(listbox("ListboxChatWindowScoreScreen", (30, 440, 400, 510), rows=4, size=11))
    kids.append(button("ButtonBuddy", (510, 520, 560, 546), None, hidden=True))
    kids.append(static("StaticTextGameSaveComplete", (580, 440, 780, 470), text("GUI:ScoreSaved", "Replay saved"), size=11, hidden=True))
    kids.append(panel("BigPortrait", (580, 380, 640, 440), fill=TRANSPARENT, border=TRANSPARENT, hidden=True))
    kids.append(static("ChallengeWinLossText", (580, 380, 780, 410), None, hidden=True))
    kids.append(static("GeneralRemarks", (580, 410, 780, 440), None, hidden=True))
    kids.append(panel("GadgetParent", (580, 480, 584, 484), fill=TRANSPARENT, border=TRANSPARENT, hidden=True))
    kids.append(button("ButtonContinue", (600, 480, 780, 516), text("GUI:Continue", "Continue"), hidden=True))
    kids.append(button("ButtonSaveReplay", (580, 480, 780, 516), text("GUI:SaveReplay", "Save replay"), size=12))
    kids.append(button("ButtonOk", (620, 548, 780, 584), text("GUI:Ok", "OK"), size=15))
    parent = full_screen_image("ParentScoreScreen", "SP_ScoreIronwood", system="ScoreScreenSystem", input="ScoreScreenInput",
                               children=kids)
    backdrop = panel("MainBackdrop", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True)
    parent.children.insert(0, backdrop)
    return write_wnd("ScoreScreen.wnd", [parent], RES, init="ScoreScreenInit", update="ScoreScreenUpdate",
                     shutdown="ScoreScreenShutdown")


# --------------------------------------------------------------------------------------------------
# small utility layouts the engine loads at startup or on demand
# --------------------------------------------------------------------------------------------------

def replay_control():
    root = panel("ParentReplayControl", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True, system="ReplayControlSystem")
    return write_wnd("ReplayControl.wnd", [root], RES)


def generate(emit):
    for path, data in sorted(generate_all().items()):
        emit(path, data)


def generate_all():
    """-> {install relative path: bytes}"""
    files = {
        "Window/Menus/BlankWindow.wnd": blank_window(),
        "Window/Menus/MainMenu.wnd": main_menu(),
        "Window/Menus/MessageBox.wnd": message_box("MessageBox.wnd"),
        "Window/Menus/QuitMessageBox.wnd": message_box("QuitMessageBox.wnd", quit_logo=True),
        "Window/Menus/QuitMenu.wnd": quit_menu("QuitMenu.wnd", "QuitMenuParent"),
        "Window/Menus/QuitNoSave.wnd": quit_menu("QuitNoSave.wnd", "QuitNoSaveParent"),
        "Window/Menus/SkirmishGameOptionsMenu.wnd": skirmish_options(),
        "Window/Menus/SkirmishMapSelectMenu.wnd": skirmish_map_select(),
        "Window/Menus/MultiplayerLoadScreen.wnd": multiplayer_load_screen(),
        "Window/Menus/ShellGameLoadScreen.wnd": shell_load_screen(),
        "Window/Menus/ScoreScreen.wnd": score_screen(),
        "Window/ReplayControl.wnd": replay_control(),
    }
    return files
