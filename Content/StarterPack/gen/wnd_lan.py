"""The network (LAN) menus as WND files: the lobby, the game setup of a LAN game, its map selection and the
small game information box next to the lobby's game list.

In the browser the "LAN" is a virtual one: everybody who joins the same room code (see
GeneralsMD/Code/Main/webnet/README.md) is on one network, and the engine's own LAN code does the rest, so these
layouts are the stock LAN layouts in the pack's flat style. Window names follow what LanLobbyMenu.cpp,
LanGameOptionsMenu.cpp, LanMapSelectMenu.cpp and GameInfoWindow.cpp look up. The labels of the engine's own LAN
messages (``LAN:...``, a few ``GUI:...``) are registered here, too.
"""

from spk.wnd import Window, Look, TRANSPARENT, write_wnd

from .textdb import text
from .wnd_widgets import (MUTED_STYLE, AMBER_STYLE, AMBER, AMBER_DIM, PANEL, STEEL, PASS_ALL, panel, label, static,
                          button, checkbox, radio, entry, listbox, combo)
from .wnd_menus import RES, MAX_SLOTS, full_screen_image

ROW_Y0 = 124
ROW_H = 34

# (label, english). ``%ls`` is a text argument, ``%d`` a number.
ENGINE_STRINGS = [
    ("GUI:Accept", "Accept"),
    ("GUI:InternetDisconnectionMenuBody1", "Waiting for the other players. If somebody does not answer, you can remove them from the game."),
    ("Network:PlayerLeftGame", "%ls left the game."),
    ("WOL:ChatErrorSerialDup", "Another player is using the same serial number."),
    ("GUI:CouldNotTransferMap", "The map could not be transferred."),
    ("GUI:HostWantsToStart", "The host wants to start the game. Press Accept when you are ready."),
    ("GUI:LocalPlayerNoMap", "You do not have the map %ls."),
    ("GUI:LocalPlayerNoMapWillTransfer", "You do not have the map %ls; it will be sent to you."),
    ("GUI:NeedHumanPlayers", "A network game needs at least one other human player."),
    ("GUI:NetworkError", "Network error"),
    ("GUI:NotifiedStartIntent", "The other players were told that you want to start."),
    ("GUI:PlayerNoMap", "%ls does not have the map %ls."),
    ("GUI:PlayerNoMapWillTransfer", "%ls does not have the map %ls; it will be sent."),
    ("GUI:SandboxMode", "All players are on the same team: nobody can win."),
    ("GUI:SocketError", "The network could not be opened. Join a room first (see the launcher page)."),
    ("LAN:ErrorBusy", "That game is busy right now. Try again."),
    ("LAN:ErrorCRCMismatch", "Your game data does not match the host's."),
    ("LAN:ErrorDuplicateName", "Another player in the game already has that name."),
    ("LAN:ErrorGameExists", "There is already a game with that name."),
    ("LAN:ErrorGameFull", "That game is full."),
    ("LAN:ErrorGameGone", "That game is no longer there."),
    ("LAN:ErrorGameStarted", "That game has already started."),
    ("LAN:ErrorNoGameSelected", "Select a game in the list first."),
    ("LAN:ErrorTimeout", "The host did not answer in time."),
    ("LAN:ErrorUnknown", "Something went wrong."),
    ("LAN:GameStartTimerPlural", "The game starts in %d seconds."),
    ("LAN:GameStartTimerSingular", "The game starts in %d second."),
    ("LAN:HostNotResponding", "The host is not responding."),
    ("LAN:JoinFailed", "Could not join the game."),
    ("LAN:NeedMorePlayers", "This map needs more players (%d so far)."),
    ("LAN:NeedMoreTeams", "There must be at least two teams."),
    ("LAN:OK", "OK"),
    ("LAN:PlayerDropped", "%ls left the game."),
    ("LAN:TooManyPlayers", "This map has room for %d players only."),
]


def _lobby():
    kids = []
    kids.append(label("StaticTextTitle", (24, 14, 520, 52), text("LAN:LobbyTitle", "Network lobby"), size=26, bold=True,
                      style=AMBER_STYLE))
    kids.append(label("StaticTextGames", (24, 58, 400, 80), text("LAN:Games", "Games"), size=11, style=MUTED_STYLE))
    kids.append(listbox("ListboxGames", (24, 82, 520, 292), rows=10, columns=1, widths=[100], size=12, forceselect=True))
    kids.append(static("StaticTextGameInfo", (536, 82, 776, 292), None))
    kids.append(label("StaticTextChat", (24, 298, 400, 320), text("LAN:Chat", "Chat"), size=11, style=MUTED_STYLE))
    kids.append(listbox("ListboxChatWindowLanLobby", (24, 322, 520, 472), rows=9, columns=1, widths=[100], size=11,
                        autoscroll=True))
    kids.append(entry("TextEntryChat", (24, 480, 440, 506), maxlen=100))
    kids.append(button("ButtonEmote", (448, 480, 520, 506), text("LAN:Send", "Send"), size=12))
    kids.append(label("StaticTextPlayers", (536, 298, 776, 320), text("LAN:PlayersHere", "Players in the lobby"), size=11,
                      style=MUTED_STYLE))
    kids.append(listbox("ListboxPlayers", (536, 322, 776, 472), rows=9, columns=1, widths=[100], size=11))
    kids.append(label("StaticTextYourName", (536, 480, 640, 506), text("LAN:YourName", "Your name"), size=11, style=MUTED_STYLE))
    kids.append(entry("TextEntryPlayerName", (630, 480, 700, 506), maxlen=20))
    kids.append(button("ButtonClear", (706, 480, 776, 506), text("LAN:ClearName", "Clear"), size=11))
    kids.append(button("ButtonBack", (24, 548, 184, 584), text("GUI:Back", "Back"), size=14))
    kids.append(button("ButtonJoin", (440, 548, 600, 584), text("LAN:Join", "Join game"), size=14))
    kids.append(button("ButtonHost", (616, 548, 776, 584), text("LAN:Host", "Host game"), size=14))
    # Typing in an address needs a layout the pack does not have; the room is the network here.
    kids.append(button("ButtonDirectConnect", (0, 0, 2, 2), None, hidden=True))
    kids.append(static("StaticToolTip", (200, 556, 420, 580), None, size=10, style=MUTED_STYLE, hidden=True))
    parent = full_screen_image("LanLobbyMenuParent", "SP_MenuBackdrop", system="LanLobbyMenuSystem",
                               input="LanLobbyMenuInput", children=kids)
    return write_wnd("LanLobbyMenu.wnd", [parent], RES, update="LanLobbyMenuUpdate", init="LanLobbyMenuInit",
                     shutdown="LanLobbyMenuShutdown")


def _game_info():
    """The box next to the lobby's game list; the code moves and resizes it onto StaticTextGameInfo."""
    kids = [
        static("StaticTextGameName", (544, 90, 768, 112), None, size=14, bold=True, style=AMBER_STYLE),
        label("StaticTextMapLabel", (544, 114, 600, 134), text("LAN:InfoMap", "Map"), size=10, style=MUTED_STYLE),
        static("StaticTextMapName", (604, 114, 768, 136), None, size=11),
        listbox("ListBoxPlayers", (544, 142, 768, 284), rows=8, columns=2, widths=[12, 88], size=11),
        # icons that code shows for the game's options; this game has none of them
        panel("WinCrates", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True),
        panel("WinSuperWeapons", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True),
        panel("WinFreeForAll", (0, 0, 2, 2), fill=TRANSPARENT, border=TRANSPARENT, hidden=True),
    ]
    parent = panel("ParentGameInfo", (536, 82, 776, 292), fill=PANEL, border=AMBER_DIM, system="GameInfoWindowSystem",
                   children=kids)
    return write_wnd("GameInfoWindow.wnd", [parent], RES, init="GameInfoWindowInit")


def _game_options():
    children = []
    children.append(label("StaticTextTitle", (24, 14, 520, 52), text("LAN:GameTitle", "Network game"), size=26, bold=True,
                          style=AMBER_STYLE))
    children.append(panel("RowsBackdrop", (16, 90, 530, ROW_Y0 + ROW_H * MAX_SLOTS + 4), fill=PANEL, border=AMBER_DIM))
    children.append(label("StaticTextPlayers", (24, 96, 170, 118), text("GUI:Players", "Player"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextFaction", (178, 96, 298, 118), text("GUI:Faction", "Faction"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextColor", (304, 96, 384, 118), text("GUI:Color", "Color"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextTeam", (390, 96, 460, 118), text("GUI:Team", "Team"), size=11, style=MUTED_STYLE))
    children.append(label("StaticTextReady", (466, 96, 526, 118), text("LAN:Ready", "Ready"), size=11, style=MUTED_STYLE))
    for i in range(MAX_SLOTS):
        y = ROW_Y0 + i * ROW_H
        children.append(combo("ComboBoxPlayer%d" % i, (24, y, 170, y + 26), display=5))
        children.append(combo("ComboBoxPlayerTemplate%d" % i, (178, y, 298, y + 26), display=5))
        children.append(combo("ComboBoxColor%d" % i, (304, y, 384, y + 26), display=8))
        children.append(combo("ComboBoxTeam%d" % i, (390, y, 460, y + 26), display=5))
        # shows green when the player pressed Accept; the code colours it
        children.append(button("ButtonAccept%d" % i, (472, y, 512, y + 26), None))
    children.append(label("StaticTextMapPreview", (548, 96, 784, 118), text("GUI:Map", "Map"), size=11, style=MUTED_STYLE))
    map_window = panel("MapWindow", (548, 120, 784, 304), fill=(10, 16, 26, 255), border=AMBER, system=PASS_ALL,
                       draw="W3DDrawMapPreview")
    for i in range(MAX_SLOTS):
        map_window.add(button("ButtonMapStartPosition%d" % i, (0, 0, 22, 22), text("GUI:StartPos%d" % i, str(i + 1)),
                              size=10, hidden=True))
    children.append(map_window)
    children.append(static("TextEntryMapDisplay", (548, 310, 784, 336), None, size=12, bold=True, centered=True,
                           fill=(10, 16, 26, 255), border=STEEL))
    children.append(button("ButtonSelectMap", (548, 344, 784, 380), text("GUI:SelectMap", "Choose map"), size=13))
    children.append(label("StaticTextStartingCash", (548, 392, 700, 414), text("GUI:StartingCash", "Starting cash"), size=11,
                          style=MUTED_STYLE))
    children.append(combo("ComboBoxStartingCash", (548, 416, 784, 442), display=5))
    children.append(checkbox("CheckboxLimitSuperweapons", (548, 452, 784, 476), text("GUI:LimitSuperweapons", "Limit superweapons")))
    # chat
    children.append(listbox("ListboxChatWindowLanGame", (24, 410, 530, 520), rows=7, columns=1, widths=[100], size=11,
                            autoscroll=True))
    children.append(entry("TextEntryChat", (24, 526, 446, 548), maxlen=100))
    children.append(button("ButtonEmote", (452, 526, 530, 548), text("LAN:Send", "Send"), size=11))
    children.append(button("ButtonBack", (24, 556, 184, 592), text("GUI:Back", "Back"), size=14))
    children.append(button("ButtonStart", (616, 556, 784, 592), text("GUI:Start", "Start"), size=16))
    parent = full_screen_image("LanGameOptionsMenuParent", "SP_MenuBackdrop", system="LanGameOptionsMenuSystem",
                               input="LanGameOptionsMenuInput", children=children)
    return write_wnd("LanGameOptionsMenu.wnd", [parent], RES, update="LanGameOptionsMenuUpdate",
                     init="LanGameOptionsMenuInit", shutdown="LanGameOptionsMenuShutdown")


def _map_select():
    kids = []
    kids.append(label("StaticTextTitle", (130, 70, 500, 100), text("GUI:ChooseMap", "Choose a map"), size=20, bold=True,
                      style=AMBER_STYLE))
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
    parent = panel("LanMapSelectMenuParent", (110, 56, 720, 540), fill=(12, 18, 30, 248), border=AMBER,
                   system="LanMapSelectMenuSystem", children=kids)
    parent.input = "LanMapSelectMenuInput"
    return write_wnd("LanMapSelectMenu.wnd", [parent], RES, update="LanMapSelectMenuUpdate", init="LanMapSelectMenuInit",
                     shutdown="LanMapSelectMenuShutdown")


def _disconnect_screen():
    """Shown in a network game when another player stops answering: who it is, how long, and a vote to remove them.
    The code shows and hides these controls by name and dereferences the buttons, so every one must exist."""
    kids = []
    kids.append(label("StaticTitle", (230, 118, 570, 146), text("GUI:DisconnectTitle", "Connection problem"), size=18, bold=True,
                      style=AMBER_STYLE, centered=True))
    kids.append(listbox("ListboxTextDisplay", (230, 152, 570, 232), rows=4, columns=1, widths=[100], size=11, autoscroll=True))
    kids.append(label("StaticPacketRouterTimeoutLabel", (240, 238, 450, 258), text("GUI:DisconnectRouterWait", "Waiting for a new host:"),
                      size=11, style=MUTED_STYLE, hidden=True))
    kids.append(static("StaticPacketRouterTimeout", (456, 238, 560, 258), None, size=11, hidden=True))
    for i in range(1, 8):
        y = 262 + (i - 1) * 24
        kids.append(static("StaticPlayer%dName" % i, (240, y, 400, y + 20), None, size=11, hidden=True))
        kids.append(static("StaticPlayer%dTimeout" % i, (404, y, 450, y + 20), None, size=11, hidden=True))
        kids.append(button("ButtonKickPlayer%d" % i, (456, y, 520, y + 20), text("GUI:DisconnectRemove", "Remove"), size=10, hidden=True))
        kids.append(static("StaticPlayer%dVotes" % i, (526, y, 560, y + 20), None, size=11, hidden=True))
    kids.append(entry("TextEntry", (240, 436, 560, 458), maxlen=100))
    kids.append(button("ButtonQuitGame", (330, 466, 470, 500), text("GUI:DisconnectQuit", "Leave game"), size=13))
    box = panel("DisconnectScreenParent", (220, 108, 580, 510), fill=(12, 18, 30, 246), border=AMBER,
                system="DisconnectControlSystem", children=kids)
    box.input = "DisconnectControlInput"
    return write_wnd("DisconnectScreen.wnd", [box], RES)


def generate_all():
    """-> {install relative path: bytes}"""
    for name, english in ENGINE_STRINGS:
        text(name, english)
    return {
        "Window/Menus/LanLobbyMenu.wnd": _lobby(),
        "Window/Menus/GameInfoWindow.wnd": _game_info(),
        "Window/Menus/LanGameOptionsMenu.wnd": _game_options(),
        "Window/Menus/LanMapSelectMenu.wnd": _map_select(),
        "Window/Menus/DisconnectScreen.wnd": _disconnect_screen(),
    }


def generate(emit):
    for path, data in sorted(generate_all().items()):
        emit(path, data)
