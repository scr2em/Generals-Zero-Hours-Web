"""The remaining screens of the game: end of game banners, credits, options, save / load, replays, in game chat and
the player list, plus the small helper windows the engine creates on its own.

Every window name below is one the engine looks up by string (``tools/check_wnd_names.py`` lists them); the
callbacks are names compiled into the engine (``FunctionLexicon``). The look follows ``wnd_menus.py``.
"""

from spk.wnd import Window, Look, TextStyle, TRANSPARENT, write_wnd

from .textdb import text
from .wnd_widgets import (INK, MUTED, AMBER, AMBER_DIM, PANEL, PANEL_LIGHT, STEEL, BLACK, TEXT_STYLE, MUTED_STYLE,
                          AMBER_STYLE, PASS_ALL, panel, label, static, button, checkbox, radio, entry, progress, listbox,
                          combo, hslider, font)
from .wnd_menus import full_screen_image, MAX_SLOTS

RES = (800, 600)
BIG = TextStyle(enabled=(INK, BLACK), disabled=(MUTED, BLACK), hilite=(INK, BLACK))


def shade(name, rect, alpha=150):
    return panel(name, rect, fill=(0, 0, 0, alpha), border=TRANSPARENT)


# --------------------------------------------------------------------------------------------------
# End of game banners (ScriptActions::doVictory / doDefeat / doLocalDefeat create these layouts)
# --------------------------------------------------------------------------------------------------

def banner(layout, parent_name, label_key, english, sub_key, sub_english, style):
    bar = panel("Bar", (0, 214, 800, 386), fill=(6, 10, 18, 214), border=AMBER, children=[
        label("LabelBanner", (0, 232, 800, 312), text(label_key, english), size=44, bold=True, centered=True, style=style),
        label("LabelBannerSub", (0, 322, 800, 360), text(sub_key, sub_english), size=16, centered=True, style=TEXT_STYLE),
    ])
    root = Window("USER", parent_name, (0, 0, 800, 600), status=("ENABLED", "NOINPUT"), look=Look.flat(TRANSPARENT))
    root.add(bar)
    return write_wnd(layout, [root], RES)


def banners():
    win = TextStyle(enabled=((120, 230, 120, 255), BLACK), disabled=(MUTED, BLACK), hilite=(INK, BLACK))
    lose = TextStyle(enabled=((235, 96, 80, 255), BLACK), disabled=(MUTED, BLACK), hilite=(INK, BLACK))
    return {
        "Window/Menus/Victorious.wnd": banner("Victorious.wnd", "VictoriousParent", "GUI:BannerVictory", "VICTORY",
                                              "GUI:BannerVictorySub", "The enemy base has fallen.", win),
        "Window/Menus/Defeat.wnd": banner("Defeat.wnd", "DefeatParent", "GUI:BannerDefeat", "DEFEAT",
                                          "GUI:BannerDefeatSub", "Your base has fallen.", lose),
        "Window/Menus/LocalDefeat.wnd": banner("LocalDefeat.wnd", "LocalDefeatParent", "GUI:BannerLocalDefeat", "YOU ARE OUT",
                                               "GUI:BannerLocalDefeatSub", "Your allies fight on.", lose),
        "Window/Menus/ObserverQuit.wnd": banner("ObserverQuit.wnd", "ObserverQuitParent", "GUI:BannerOver", "GAME OVER",
                                                "GUI:BannerOverSub", "The match has ended.", BIG),
    }


# --------------------------------------------------------------------------------------------------
# Windows the engine creates for itself at start up
# --------------------------------------------------------------------------------------------------

def ime_windows():
    """Input method editor windows (IMEManager::init). They stay hidden unless an input method is active."""
    cand = Window("USER", "IMECandidateWindow", (0, 0, 220, 120), status=("ENABLED",), system="IMECandidateWindowSystem",
                  input="IMECandidateWindowInput", draw="IMECandidateMainDraw", look=Look.flat((16, 24, 36, 235), STEEL))
    cand.add(Window("USER", "TextArea", (4, 4, 216, 100), status=("ENABLED",), draw="IMECandidateTextAreaDraw",
                    look=Look.flat(TRANSPARENT), font=font(12)),
             Window("USER", "UpArrow", (4, 102, 20, 118), status=("ENABLED",), look=Look.flat(TRANSPARENT)),
             Window("USER", "DownArrow", (200, 102, 216, 118), status=("ENABLED",), look=Look.flat(TRANSPARENT)))
    status = Window("USER", "IMEStatusWindow", (0, 0, 40, 24), status=("ENABLED",), look=Look.flat((16, 24, 36, 235), STEEL))
    return {"Window/IMECandidateWindow.wnd": write_wnd("IMECandidateWindow.wnd", [cand], RES),
            "Window/IMEStatusWindow.wnd": write_wnd("IMEStatusWindow.wnd", [status], RES)}


def in_game_popup_message():
    parent = panel("InGamePopupMessageParent", (200, 200, 600, 330), fill=PANEL_LIGHT, border=AMBER, hidden=True,
                   system="InGamePopupMessageSystem", children=[
                       static("StaticTextMessage", (204, 204, 596, 290), None, size=12, centered=True),
                       button("ButtonOk", (500, 296, 596, 324), text("GUI:Ok", "OK"), size=12),
                   ])
    parent.input = "InGamePopupMessageInput"
    return write_wnd("InGamePopupMessage.wnd", [parent], RES, init="InGamePopupMessageInit")


# --------------------------------------------------------------------------------------------------
# In game chat and the player list
# --------------------------------------------------------------------------------------------------

def in_game_chat():
    parent = panel("ChatParent", (180, 396, 620, 432), fill=(8, 12, 20, 220), border=AMBER_DIM, system="InGameChatSystem",
                   children=[
                       static("StaticTextChatType", (186, 402, 262, 426), None, size=12, bold=True, style=AMBER_STYLE),
                       entry("TextEntryChat", (266, 402, 580, 426), maxlen=120),
                       button("ButtonClear", (584, 402, 614, 426), text("GUI:ClearChat", "X"), size=11),
                   ])
    parent.input = "InGameChatInput"
    return write_wnd("InGameChat.wnd", [parent], RES)


def diplomacy():
    rows = []
    y0 = 118
    for i in range(MAX_SLOTS):
        y = y0 + i * 30
        rows.append(static("StaticTextPlayer%d" % i, (232, y, 400, y + 24), None, size=12, bold=True, hidden=True))
        rows.append(static("StaticTextSide%d" % i, (404, y, 524, y + 24), None, size=12, hidden=True))
        rows.append(static("StaticTextTeam%d" % i, (528, y, 604, y + 24), None, size=12, hidden=True))
        rows.append(static("StaticTextStatus%d" % i, (608, y, 700, y + 24), None, size=12, hidden=True))
        rows.append(button("ButtonMute%d" % i, (704, y, 764, y + 24), text("GUI:Mute", "Mute"), size=10, hidden=True))
        rows.append(button("ButtonUnMute%d" % i, (704, y, 764, y + 24), text("GUI:Unmute", "Unmute"), size=10, hidden=True))
    heads = [label("HeadPlayer", (232, 92, 400, 114), text("GUI:Player", "Player"), size=10, style=MUTED_STYLE),
             label("HeadSide", (404, 92, 524, 114), text("GUI:Faction", "Faction"), size=10, style=MUTED_STYLE),
             label("HeadTeam", (528, 92, 604, 114), text("GUI:Team", "Team"), size=10, style=MUTED_STYLE),
             label("HeadStatus", (608, 92, 700, 114), text("GUI:DiplomacyStatus", "Status"), size=10, style=MUTED_STYLE)]
    in_game = panel("InGameParent", (224, 84, 776, 366), fill=TRANSPARENT, border=TRANSPARENT, children=heads + rows)
    solo = panel("SoloParent", (224, 84, 776, 366), fill=TRANSPARENT, border=TRANSPARENT, hidden=True, children=[
        listbox("ListboxSolo", (232, 96, 768, 358), rows=12, size=11)])
    buddies = panel("BuddiesParent", (224, 84, 776, 366), fill=TRANSPARENT, border=TRANSPARENT, hidden=True, children=[
        listbox("ListboxBuddies", (232, 96, 380, 358), rows=12, size=11),
        listbox("ListboxBuddyChat", (388, 96, 768, 320), rows=10, size=11),
        entry("TextEntryChat", (388, 326, 768, 352), maxlen=100)])
    parent = panel("DiplomacyParent", (216, 40, 784, 424), fill=PANEL_LIGHT, border=AMBER, system="DiplomacySystem", children=[
        label("LabelDiplomacy", (232, 48, 600, 80), text("GUI:DiplomacyTitle", "Players"), size=20, bold=True, style=AMBER_STYLE),
        radio("RadioButtonInGame", (560, 52, 660, 76), text("GUI:DiplomacyInGame", "In game"), group=2),
        radio("RadioButtonBuddies", (664, 52, 776, 76), text("GUI:DiplomacyBuddies", "Friends"), group=2),
        in_game, solo, buddies,
        button("ButtonHide", (640, 378, 772, 414), text("GUI:Close", "Close"), size=13)])
    parent.input = "DiplomacyInput"
    return write_wnd("Diplomacy.wnd", [parent], RES)


# --------------------------------------------------------------------------------------------------
# Credits
# --------------------------------------------------------------------------------------------------

def credits_menu():
    # the credits scroll is drawn by the parent window's draw callback (W3DCreditsMenuDraw); Escape pops the screen
    # a window with its own draw function does not draw its picture, so the backdrop is a second window below it
    backdrop = full_screen_image("CreditsBackdrop", "SP_MenuBackdrop", status=("ENABLED", "NOINPUT"))
    backdrop.add(shade("Veil", (0, 0, 800, 600), 150))
    parent = Window("USER", "ParentCreditsWindow", (0, 0, 800, 600), status=("ENABLED",), system="CreditsMenuSystem",
                    input="CreditsMenuInput", draw="W3DCreditsMenuDraw", look=Look.flat(TRANSPARENT))
    parent.add(label("LabelCreditsHint", (0, 568, 800, 590), text("GUI:CreditsHint", "Press Escape to return"), size=10,
                     style=MUTED_STYLE, centered=True))
    return write_wnd("CreditsMenu.wnd", [backdrop, parent], RES, init="CreditsMenuInit", update="CreditsMenuUpdate",
                     shutdown="CreditsMenuShutdown")


# --------------------------------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------------------------------

def options_menu():
    kids = []
    kids.append(label("LabelTitle", (28, 14, 500, 52), text("GUI:OptionsTitle", "Options"), size=26, bold=True, style=AMBER_STYLE))
    kids.append(static("LabelVersion", (560, 22, 780, 44), None, size=10, style=MUTED_STYLE))

    # --- left: controls
    kids.append(panel("PanelControls", (24, 62, 392, 500), fill=PANEL, border=AMBER_DIM))
    kids.append(label("LabelControls", (36, 68, 380, 92), text("GUI:OptionsControls", "Controls"), size=13, bold=True, style=AMBER_STYLE))
    y = 98
    for name, key, eng in (
            ("CheckAlternateMouse", "GUI:OptionsAltMouse", "Alternate mouse buttons"),
            ("Retaliation", "GUI:OptionsRetaliation", "Units fight back when attacked"),
            ("CheckDoubleClickAttackMove", "GUI:OptionsDoubleClick", "Double click orders an attack move"),
            ("CheckBoxDrawAnchor", "GUI:OptionsDrawAnchor", "Draw the scroll anchor"),
            ("CheckBoxMoveAnchor", "GUI:OptionsMoveAnchor", "Move the scroll anchor with the mouse"),
            ("CheckLanguageFilter", "GUI:OptionsLanguageFilter", "Filter bad words in chat"),
            ("CheckBoxSaveCamera", "GUI:OptionsSaveCamera", "Record the camera in replays"),
            ("CheckBoxUseCamera", "GUI:OptionsUseCamera", "Use the recorded camera in replays")):
        kids.append(checkbox(name, (40, y, 380, y + 28), text(key, eng), size=11))
        y += 34
    kids.append(label("LabelScroll", (40, y + 8, 380, y + 30), text("GUI:OptionsScrollSpeed", "Scroll speed"), size=11, style=MUTED_STYLE))
    kids.append(hslider("SliderScrollSpeed", (40, y + 34, 380, y + 54), 1, 100))
    kids.append(button("ButtonKeyboardOptions", (40, y + 70, 380, y + 100), text("GUI:OptionsKeyboard", "Keyboard shortcuts..."), size=12))

    # --- right: sound and picture
    kids.append(panel("PanelAudio", (408, 62, 776, 500), fill=PANEL, border=AMBER_DIM))
    kids.append(label("LabelAudio", (420, 68, 760, 92), text("GUI:OptionsAudio", "Sound"), size=13, bold=True, style=AMBER_STYLE))
    y = 98
    for name, key, eng in (("SliderMusicVolume", "GUI:OptionsMusic", "Music"), ("SliderSFXVolume", "GUI:OptionsSfx", "Effects"),
                           ("SliderVoiceVolume", "GUI:OptionsVoice", "Voices")):
        kids.append(label("Label" + name[6:], (424, y, 560, y + 22), text(key, eng), size=11, style=MUTED_STYLE))
        kids.append(hslider(name, (540, y + 2, 756, y + 22), 0, 100))
        y += 32
    kids.append(label("LabelVideo", (420, y + 6, 760, y + 30), text("GUI:OptionsVideo", "Picture"), size=13, bold=True, style=AMBER_STYLE))
    y += 38
    kids.append(label("LabelGamma", (424, y, 560, y + 22), text("GUI:OptionsGamma", "Brightness"), size=11, style=MUTED_STYLE))
    kids.append(hslider("SliderGamma", (540, y + 2, 756, y + 22), 0, 100))
    y += 34
    kids.append(label("LabelResolution", (424, y, 540, y + 22), text("GUI:Resolution", "Resolution"), size=11, style=MUTED_STYLE))
    kids.append(combo("ComboBoxResolution", (540, y, 756, y + 26), display=5))
    y += 36
    kids.append(label("LabelDetail", (424, y, 540, y + 22), text("GUI:OptionsDetail", "Detail"), size=11, style=MUTED_STYLE))
    kids.append(combo("ComboBoxDetail", (540, y, 756, y + 26), display=5))
    y += 36
    kids.append(label("LabelAA", (424, y, 540, y + 22), text("GUI:OptionsAntiAliasing", "Smooth edges"), size=11, style=MUTED_STYLE))
    kids.append(combo("ComboBoxAntiAliasing", (540, y, 756, y + 26), display=5))

    # network settings the starter content does not offer: the engine looks the controls up, keep them out of sight
    kids.append(combo("ComboBoxIP", (0, 0, 8, 8), display=2))
    kids.append(combo("ComboBoxOnlineIP", (0, 0, 8, 8), display=2))
    kids.append(button("ButtonFirewallRefresh", (0, 0, 8, 8), None, hidden=True))
    for w in kids[-3:]:
        if "HIDDEN" not in w.status:
            w.status.append("HIDDEN")

    # --- buttons
    kids.append(button("ButtonDefaults", (24, 548, 184, 584), text("GUI:OptionsDefaults", "Defaults"), size=14))
    kids.append(button("ButtonBack", (460, 548, 620, 584), text("GUI:Cancel", "Cancel"), size=14))
    kids.append(button("ButtonAccept", (624, 548, 784, 584), text("GUI:OptionsAccept", "Accept"), size=15))

    # --- the "custom detail" pop-up: individual picture settings
    adv = []
    items = [("Check3DShadows", "GUI:Opt3DShadows", "Volume shadows"), ("Check2DShadows", "GUI:Opt2DShadows", "Shadow decals"),
             ("CheckCloudShadows", "GUI:OptCloudShadows", "Cloud shadows"), ("CheckGroundLighting", "GUI:OptGroundLighting", "Ground lighting"),
             ("CheckSmoothWater", "GUI:OptSmoothWater", "Soft water edges"), ("CheckExtraAnimations", "GUI:OptExtraAnim", "Extra animations"),
             ("CheckNoDynamicLOD", "GUI:OptNoDynLod", "Always full model detail"), ("CheckHeatEffects", "GUI:OptHeat", "Heat haze"),
             ("CheckUnlockFPS", "GUI:OptUnlockFps", "Unlock the frame rate"), ("CheckBehindBuilding", "GUI:OptBehind", "Show units behind buildings"),
             ("CheckShowProps", "GUI:OptProps", "Show trees and props")]
    yy = 88
    for name, key, eng in items:
        adv.append(checkbox(name, (244, yy, 560, yy + 28), text(key, eng), size=11))
        yy += 29
    adv.append(label("LabelLowRes", (244, yy + 6, 400, yy + 28), text("GUI:OptTextures", "Texture detail"), size=11, style=MUTED_STYLE))
    adv.append(hslider("LowResSlider", (400, yy + 8, 556, yy + 28), 0, 2))
    adv.append(label("LabelParticles", (244, yy + 36, 400, yy + 58), text("GUI:OptParticles", "Particles"), size=11, style=MUTED_STYLE))
    adv.append(hslider("ParticleCapSlider", (400, yy + 38, 556, yy + 58), 100, 3000))
    adv.append(button("ButtonAdvanceBack", (240, yy + 76, 396, yy + 108), text("GUI:Cancel", "Cancel"), size=12))
    adv.append(button("ButtonAdvanceAccept", (404, yy + 76, 560, yy + 108), text("GUI:Ok", "OK"), size=12))
    kids.append(panel("WinAdvancedDisplayOptions", (220, 20, 580, yy + 124), fill=PANEL_LIGHT, border=AMBER, hidden=True,
                      children=[label("LabelAdvanced", (236, 28, 560, 58), text("GUI:OptCustomTitle", "Custom detail"), size=15,
                                      bold=True, style=AMBER_STYLE)] + adv))

    parent = full_screen_image("OptionsMenuParent", "SP_MenuBackdrop", system="OptionsMenuSystem", input="OptionsMenuInput",
                               children=kids)
    return write_wnd("OptionsMenu.wnd", [parent], RES, init="OptionsMenuInit", update="OptionsMenuUpdate",
                     shutdown="OptionsMenuShutdown")


def keyboard_menu():
    kids = [
        label("LabelTitle", (28, 14, 600, 52), text("GUI:KeysTitle", "Keyboard shortcuts"), size=26, bold=True, style=AMBER_STYLE),
        label("LabelCategory", (28, 70, 200, 92), text("GUI:KeysCategory", "Category"), size=11, style=MUTED_STYLE),
        combo("ComboBoxCategoryList", (28, 94, 300, 120), display=8),
        listbox("ListBoxCommandList", (28, 132, 400, 500), rows=16, size=11, forceselect=True),
        panel("PanelDetails", (420, 94, 776, 500), fill=PANEL, border=AMBER_DIM),
        label("LabelDesc", (436, 102, 760, 124), text("GUI:KeysDescription", "What it does"), size=11, style=MUTED_STYLE),
        static("StaticTextDescription", (436, 126, 760, 220), None, size=12),
        label("LabelCurrent", (436, 232, 760, 254), text("GUI:KeysCurrent", "Current shortcut"), size=11, style=MUTED_STYLE),
        static("StaticTextCurrentHotkey", (436, 256, 760, 284), None, size=15, bold=True, style=AMBER_STYLE),
        label("LabelAssign", (436, 304, 760, 326), text("GUI:KeysAssign", "Press the new key here"), size=11, style=MUTED_STYLE),
        entry("TextEntryAssignHotkey", (436, 330, 760, 358), maxlen=2),
        button("ButtonAssign", (436, 372, 596, 406), text("GUI:KeysAssignButton", "Assign"), size=13),
        button("ButtonResetAll", (600, 372, 760, 406), text("GUI:KeysResetAll", "Reset all"), size=13),
        button("ButtonBack", (24, 548, 184, 584), text("GUI:Back", "Back"), size=14),
    ]
    parent = full_screen_image("ParentKeyboardOptionsMenu", "SP_MenuBackdrop", system="KeyboardOptionsMenuSystem",
                               input="KeyboardOptionsMenuInput", children=kids)
    return write_wnd("KeyboardOptionsMenu.wnd", [parent], RES, init="KeyboardOptionsMenuInit",
                     shutdown="KeyboardOptionsMenuShutdown")


# --------------------------------------------------------------------------------------------------
# Save and load (full screen from the main menu, pop-up from the in game menu), replays
# --------------------------------------------------------------------------------------------------

def _confirm(name, rect, title_key, title_eng, yes_name, yes_key, yes_eng, no_name, no_key, no_eng, extra=()):
    x0, y0, x1, y1 = rect
    mid = (x0 + x1) // 2
    return panel(name, rect, fill=PANEL_LIGHT, border=AMBER, hidden=True, children=[
        label(name + "Title", (x0 + 8, y0 + 8, x1 - 8, y0 + 56), text(title_key, title_eng), size=13, centered=True),
        *extra,
        button(yes_name, (x0 + 14, y1 - 46, mid - 6, y1 - 12), text(yes_key, yes_eng), size=13),
        button(no_name, (mid + 6, y1 - 46, x1 - 14, y1 - 12), text(no_key, no_eng), size=13)])


def save_load(layout, fullscreen):
    frame = panel("MenuButtonFrame", (24, 470, 776, 586), fill=TRANSPARENT, border=TRANSPARENT, children=[
        button("ButtonSave", (30, 484, 220, 520), text("GUI:SaveGame", "Save game"), size=14),
        button("ButtonLoad", (232, 484, 422, 520), text("GUI:LoadGame", "Load game"), size=14),
        button("ButtonDelete", (434, 484, 624, 520), text("GUI:DeleteGame", "Delete"), size=14),
        button("ButtonBack", (30, 534, 220, 570), text("GUI:Back", "Back"), size=14)])
    kids = [
        label("LabelTitle", (28, 14, 600, 52), text("GUI:SaveLoadTitle", "Saved games"), size=26, bold=True, style=AMBER_STYLE),
        panel("ListBackdrop", (24, 62, 776, 464), fill=PANEL, border=AMBER_DIM),
        label("HeadName", (40, 68, 400, 88), text("GUI:SaveHeadName", "Game"), size=10, style=MUTED_STYLE),
        label("HeadTime", (470, 68, 580, 88), text("GUI:SaveHeadTime", "Time"), size=10, style=MUTED_STYLE),
        label("HeadDate", (610, 68, 760, 88), text("GUI:SaveHeadDate", "Date"), size=10, style=MUTED_STYLE),
        listbox("ListboxGames", (32, 90, 768, 458), rows=15, columns=3, widths=[56, 20, 24], size=12, forceselect=True),
        frame,
        _confirm("OverwriteConfirmParent", (220, 220, 580, 360), "GUI:SaveOverwriteTitle", "Replace this saved game?",
                 "ButtonOverwriteConfirm", "GUI:Yes", "Yes", "ButtonOverwriteCancel", "GUI:No", "No"),
        _confirm("LoadConfirmParent", (220, 220, 580, 360), "GUI:LoadConfirmTitle", "Load this game and leave the current one?",
                 "ButtonLoadConfirm", "GUI:Yes", "Yes", "ButtonLoadCancel", "GUI:No", "No"),
        _confirm("DeleteConfirmParent", (220, 220, 580, 360), "GUI:DeleteConfirmTitle", "Delete this saved game?",
                 "ButtonDeleteConfirm", "GUI:Yes", "Yes", "ButtonDeleteCancel", "GUI:No", "No"),
        _confirm("SaveDescParent", (200, 220, 600, 380), "GUI:SaveDescTitle", "Name this saved game",
                 "ButtonSaveDescConfirm", "GUI:Ok", "OK", "ButtonSaveDescCancel", "GUI:Cancel", "Cancel",
                 extra=[entry("EntryDesc", (216, 280, 584, 308), maxlen=40)]),
    ]
    if fullscreen:
        parent = full_screen_image("SaveLoadMenu", "SP_MenuBackdrop", system="SaveLoadMenuSystem", input="SaveLoadMenuInput",
                                   children=kids)
        init = "SaveLoadMenuFullScreenInit"
    else:
        parent = panel("SaveLoadMenu", (0, 0, 800, 600), fill=(0, 0, 0, 200), border=TRANSPARENT, system="SaveLoadMenuSystem",
                       children=kids)
        parent.input = "SaveLoadMenuInput"
        # the pop-up sits on top of the game: give its page a solid panel behind the list
        init = "SaveLoadMenuInit"
    return write_wnd(layout, [parent], RES, init=init, update="SaveLoadMenuUpdate", shutdown="SaveLoadMenuShutdown")


def replay_menu():
    kids = [
        label("LabelTitle", (28, 14, 600, 52), text("GUI:ReplayTitle", "Replays"), size=26, bold=True, style=AMBER_STYLE),
        panel("ListBackdrop", (24, 62, 776, 464), fill=PANEL, border=AMBER_DIM),
        label("HeadName", (40, 68, 300, 88), text("GUI:ReplayHeadName", "Replay"), size=10, style=MUTED_STYLE),
        label("HeadDate", (310, 68, 480, 88), text("GUI:ReplayHeadDate", "Date"), size=10, style=MUTED_STYLE),
        label("HeadVersion", (490, 68, 590, 88), text("GUI:ReplayHeadVersion", "Version"), size=10, style=MUTED_STYLE),
        label("HeadMap", (600, 68, 760, 88), text("GUI:ReplayHeadMap", "Map"), size=10, style=MUTED_STYLE),
        listbox("ListboxReplayFiles", (32, 90, 768, 458), rows=15, columns=4, widths=[34, 24, 14, 28], size=12, forceselect=True),
        button("ButtonLoadReplay", (30, 484, 220, 520), text("GUI:ReplayPlay", "Play"), size=14),
        button("ButtonDeleteReplay", (232, 484, 422, 520), text("GUI:ReplayDelete", "Delete"), size=14),
        button("ButtonCopyReplay", (434, 484, 624, 520), text("GUI:ReplayCopy", "Copy to Documents"), size=13),
        button("ButtonBack", (30, 534, 220, 570), text("GUI:Back", "Back"), size=14),
        panel("GadgetParent", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True),
    ]
    parent = full_screen_image("ParentReplayMenu", "SP_MenuBackdrop", system="ReplayMenuSystem", input="ReplayMenuInput",
                               children=kids)
    return write_wnd("ReplayMenu.wnd", [parent], RES, init="ReplayMenuInit", update="ReplayMenuUpdate",
                     shutdown="ReplayMenuShutdown")


def popup_replay():
    frame = panel("MenuButtonFrame", (220, 330, 580, 380), fill=TRANSPARENT, border=TRANSPARENT, children=[
        button("ButtonSave", (230, 336, 390, 372), text("GUI:ReplaySave", "Save replay"), size=13),
        button("ButtonBack", (410, 336, 570, 372), text("GUI:Back", "Back"), size=13)])
    saved = panel("PopupReplaySaved", (270, 250, 530, 330), fill=PANEL_LIGHT, border=AMBER, hidden=True, children=[
        label("LabelSaved", (276, 270, 524, 300), text("GUI:ReplaySaved", "Replay saved."), size=14, centered=True)])
    box = panel("PopupReplayMenu", (200, 90, 600, 400), fill=PANEL_LIGHT, border=AMBER, system="PopupReplaySystem", children=[
        label("LabelTitle", (212, 98, 590, 128), text("GUI:ReplaySaveTitle", "Save this game as a replay"), size=16, bold=True,
              style=AMBER_STYLE),
        listbox("ListboxGames", (212, 134, 588, 290), rows=8, size=11, forceselect=True),
        entry("TextEntryReplayName", (212, 296, 588, 322), maxlen=40),
        frame, saved])
    box.input = "PopupReplayInput"
    return write_wnd("PopupReplay.wnd", [box], RES, init="PopupReplayInit", update="PopupReplayUpdate",
                     shutdown="PopupReplayShutdown")


# --------------------------------------------------------------------------------------------------

def generate_all():
    files = {
        "Window/Menus/CreditsMenu.wnd": credits_menu(),
        "Window/Menus/OptionsMenu.wnd": options_menu(),
        "Window/Menus/KeyboardOptionsMenu.wnd": keyboard_menu(),
        "Window/Menus/SaveLoad.wnd": save_load("SaveLoad.wnd", True),
        "Window/Menus/PopupSaveLoad.wnd": save_load("PopupSaveLoad.wnd", False),
        "Window/Menus/ReplayMenu.wnd": replay_menu(),
        "Window/Menus/PopupReplay.wnd": popup_replay(),
        "Window/InGameChat.wnd": in_game_chat(),
        "Window/Diplomacy.wnd": diplomacy(),
        "Window/InGamePopupMessage.wnd": in_game_popup_message(),
    }
    files.update(banners())
    files.update(ime_windows())
    return files


def generate(emit):
    for path, data in sorted(generate_all().items()):
        emit(path, data)
