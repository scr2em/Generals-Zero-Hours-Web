"""``Data/Scripts/MultiplayerScripts.scb``: the scripts that end a skirmish or network game.

GameLogic::startNewGame reads this file for every game with at least two teams (GameLogic.cpp, "add in the
multiplayer victory/defeat scripts") and appends its scripts to the neutral side. Without them the engine tracks
victory and defeat (VictoryConditions.cpp) but nothing ever shows the banner or ends the game, so the score screen
is never reached. Three scripts, each evaluated for the local player:

* ``MultiplayerVictory``: ``MULTIPLAYER_ALLIED_VICTORY`` -> ``VICTORY`` (banner, then the game ends)
* ``MultiplayerDefeat``: ``MULTIPLAYER_ALLIED_DEFEAT`` -> ``DEFEAT`` (banner, then the game ends)
* ``MultiplayerPlayerDefeat``: ``MULTIPLAYER_PLAYER_DEFEAT`` -> ``LOCALDEFEAT`` (this player lost but allies play on)

The banners are the layouts ``Menus/Victorious.wnd``, ``Defeat.wnd``, ``LocalDefeat.wnd`` and ``ObserverQuit.wnd``
(gen/wnd_extra.py).
"""

from spk.datachunk import ChunkWriter
from spk.scripts import Script, ScriptList, action, condition, write_player_scripts


def scripts():
    return [
        Script("MultiplayerVictory", conditions=[[condition("MULTIPLAYER_ALLIED_VICTORY")]], actions=[action("VICTORY")],
               one_shot=True, comment="The local player and the allies are the last ones standing."),
        Script("MultiplayerDefeat", conditions=[[condition("MULTIPLAYER_ALLIED_DEFEAT")]], actions=[action("DEFEAT")],
               one_shot=True, comment="The local player and the allies have been wiped out."),
        Script("MultiplayerPlayerDefeat", conditions=[[condition("MULTIPLAYER_PLAYER_DEFEAT")]], actions=[action("LOCALDEFEAT")],
               one_shot=True, comment="The local player is out but allies are still playing."),
    ]


def build():
    w = ChunkWriter()
    write_player_scripts(w, [ScriptList(scripts())])
    return w.to_bytes()


def generate(emit):
    emit("Data/Scripts/MultiplayerScripts.scb", build())
