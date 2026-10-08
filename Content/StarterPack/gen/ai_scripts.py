"""Skirmish scripts: what the computer opponent builds and what every player gets at the start of a match.

``Data/Scripts/SkirmishScripts.scb`` is read by SidesList::prepareForMP_or_Skirmish when the chosen map does not carry
scripts of its own. It holds, per skirmish side of the map (the names must match the side names in the map file):

* ``SkirmishIronwood``  the opponent. Its team prototypes (the ``ScriptTeams`` chunk) tell the AI which groups of units
  to train, each gated by a production condition script; when a group is complete the engine runs the team's
  ``teamOnCreateScript`` and the group goes hunting. Structures come from ``SkirmishBuildList`` in AIData.ini.
* ``Civilian``  whose scripts go to the human player: here, starting the battle music.

When a side is instantiated the engine appends the start position index to script, team and counter names and
substitutes the real player name for parameters of kind SIDE (Parameter::qualify), so nothing here may depend on a
concrete player name.
"""

from spk.datachunk import Dict
from spk.scripts import (Script, action, condition, write_skirmish_file, THIS_TEAM, EQUAL)

AI_SIDE = "SkirmishIronwood"
HUMAN_SIDE = "Civilian"

# (team name, [(unit, min, max)...], production priority, max simultaneous instances, production condition script)
TEAMS = [
    ("TeamIronwoodRiflemen", [("IronwoodRifleman", 3, 4)], 1, 3, "IronwoodBuildRiflemen"),
    ("TeamIronwoodRockets", [("IronwoodRocketeer", 2, 3), ("IronwoodRifleman", 1, 2)], 2, 2, "IronwoodBuildRockets"),
    ("TeamIronwoodArmor", [("IronwoodTank", 2, 3), ("IronwoodScout", 1, 1)], 3, 3, "IronwoodBuildArmor"),
]


def ai_scripts():
    scripts = []
    for _name, _units, _prio, _max, cond_script in TEAMS:
        # the production condition: always true. What limits the AI is money, an idle factory that can train the
        # units, and the number of live teams of that kind.
        scripts.append(Script(cond_script, conditions=[[condition("CONDITION_TRUE")]], actions=[], one_shot=False,
                              comment="Production condition for a team of the starter army."))
    # called when a team has been fully trained: send it after the enemy
    scripts.append(Script("IronwoodAttackWave", conditions=[[condition("CONDITION_TRUE")]],
                          actions=[action("TEAM_HUNT", THIS_TEAM)], subroutine=True, one_shot=False,
                          comment="A finished team hunts the nearest enemy."))
    return scripts


def human_scripts():
    return [Script("StarterBattleMusic", conditions=[[condition("CONDITION_TRUE")]],
                   actions=[action("MUSIC_SET_TRACK", "StarterBattleMusic", False, True)], one_shot=True,
                   comment="Start the battle theme when the match begins.")]


def teams():
    # The engine names a player's default team "team<player name>" and, for a skirmish side, builds the player's
    # name from the side name plus the start position, so the file must carry the side's default team to copy.
    out = [Dict(teamName="team" + AI_SIDE, teamOwner=AI_SIDE, teamIsSingleton=True)]
    for name, units, priority, max_instances, cond_script in TEAMS:
        d = Dict()
        d.set("teamName", name)
        d.set("teamOwner", AI_SIDE)
        d.set("teamIsSingleton", False)
        for i, (unit, lo, hi) in enumerate(units, 1):
            d.set("teamUnitType%d" % i, unit)
            d.set("teamUnitMinCount%d" % i, lo)
            d.set("teamUnitMaxCount%d" % i, hi)
        d.set("teamMaxInstances", max_instances)
        d.set("teamProductionPriority", priority)
        d.set("teamProductionPrioritySuccessIncrease", 1)
        d.set("teamProductionPriorityFailureDecrease", 1)
        d.set("teamProductionCondition", cond_script)
        d.set("teamOnCreateScript", "IronwoodAttackWave")
        d.set("teamAutoReinforce", False)
        d.set("teamIsAIRecruitable", False)
        d.set("teamExecutesActionsOnCreate", False)
        out.append(d)
    return out


def build():
    return write_skirmish_file([(AI_SIDE, ai_scripts()), (HUMAN_SIDE, human_scripts())], teams())


def generate(emit):
    emit("Data/Scripts/SkirmishScripts.scb", build())
