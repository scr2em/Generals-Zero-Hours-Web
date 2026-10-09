"""Synthetic test data written from scratch for these tests: a small made-up "retail" game and a made-up mod with
several armies installed on top of it. Nothing here is taken from any real game or mod."""

import os
import struct

from zharmy import scb as scbmod
from zharmy import strings as stringsmod
from zharmy.bigfile import BigWriter

def rd(path):
    with open(path, "rb") as handle:
        return handle.read()


def wr(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


# ---- tiny W3D files ----------------------------------------------------------------------------------------------


def _name(text, n=16):
    raw = text.encode("latin-1")
    return raw + b"\0" * (n - len(raw))


def _chunk(ctype, payload=b"", children=None):
    if children is not None:
        body = b"".join(children)
        return struct.pack("<II", ctype, len(body) | 0x80000000) + body
    return struct.pack("<II", ctype, len(payload)) + payload


def w3d_mesh(container, mesh, texture):
    header = struct.pack("<II", (4 << 16) | 2, 0) + _name(mesh) + _name(container) + struct.pack("<9I", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    header += struct.pack("<10f", *([0.0] * 10))
    tex = _chunk(0x30, children=[_chunk(0x31, children=[_chunk(0x32, texture.encode() + b"\0")])]) if texture else b""
    return _chunk(0x00, children=[_chunk(0x1F, header)] + ([tex] if tex else []))


def w3d_hierarchy(name):
    hdr = struct.pack("<I", (4 << 16) | 1) + _name(name) + struct.pack("<I3f", 1, 0, 0, 0)
    pivot = _name("ROOTTRANSFORM") + struct.pack("<I", 0xFFFFFFFF) + struct.pack("<3f3f4f", 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)
    return _chunk(0x100, children=[_chunk(0x101, hdr), _chunk(0x102, pivot)])


def w3d_hlod(name, hierarchy, subobjects):
    hdr = struct.pack("<II", 1 << 16, 1) + _name(name) + _name(hierarchy)
    subs = [_chunk(0x703, struct.pack("<If", len(subobjects), 0.0))]
    for bone, full in subobjects:
        subs.append(_chunk(0x704, struct.pack("<I", bone) + _name(full, 32)))
    return _chunk(0x700, children=[_chunk(0x701, hdr), _chunk(0x702, children=subs)])


def w3d_model(name, texture, hierarchy=None, extra_mesh="BODY"):
    """A model: one mesh ``name.BODY``, optionally housecolor, a hierarchy (own or referenced) and an HLOD."""
    parts = [w3d_mesh(name, extra_mesh, texture)]
    hname = hierarchy or name
    if hierarchy is None:
        parts.append(w3d_hierarchy(name))
    parts.append(w3d_hlod(name, hname, [(0, "%s.%s" % (name, extra_mesh))]))
    return b"".join(parts)


def w3d_skeleton(name):
    return w3d_hierarchy(name)


def w3d_anim(anim, hierarchy):
    hdr = struct.pack("<I", (4 << 16) | 1) + _name(anim) + _name(hierarchy) + struct.pack("<II", 10, 30)
    return _chunk(0x200, children=[_chunk(0x201, hdr)])


# ---- INI text ------------------------------------------------------------------------------------------------------
def base_ini():
    ini = {}
    ini["Data\\INI\\Object.ini"] = """
; made-up stock units
Object TstProjectile
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = tst_shell
    End
  End
End

Object TstBaseHQ
  Side = TstBase
  DisplayName = OBJECT:TstBaseHQ
  ButtonImage = TST_HQ
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = tst_hq
    End
  End
  KindOf = STRUCTURE SELECTABLE COMMANDCENTER
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1000.0
  End
  CommandSet = TstHQSet
  BuildCost = 1000
End

Object TstBaseTank
  Side = TstBase
  DisplayName = OBJECT:TstBaseTank
  ButtonImage = TST_Tank
  VoiceSelect = TstSelect
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = tst_tank
      Animation = TST_SKL.TST_WALK
    End
  End
  KindOf = VEHICLE SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 300.0
  End
  ArmorSet
    Conditions = None
    Armor = TstArmor
    DamageFX = None
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY TstCannon
  End
  Locomotor = SET_NORMAL TstTreads
  Prerequisites
    Object = TstBaseHQ
    Science = SCIENCE_TstA
  End
  Behavior = FXListDie ModuleTag_Die
    DeathFX = FX_TstBoom
  End
  Behavior = AIUpdateInterface ModuleTag_AI
    Turret
      TurretTurnRate = 60
    End
  End
End

Object TstBaseDozer
  Side = TstBase
  DisplayName = OBJECT:TstBaseDozer
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = tst_tank
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200.0
  End
End
"""
    ini["Data\\INI\\Object\\TstExtra.ini"] = """
Object TstCrate
  EditorSorting = MISC_MAN_MADE
End
"""
    ini["Data\\INI\\Weapon.ini"] = """
Weapon TstCannon
  PrimaryDamage = 40.0
  ProjectileObject = TstProjectile
  FireFX = FX_TstFire
  FireSound = TstCannonSnd
End

Weapon TstMG
  PrimaryDamage = 5.0
  FireSound = TstCannonSnd
End
"""
    ini["Data\\INI\\Armor.ini"] = """
Armor TstArmor
  Armor = DEFAULT 100%
End
"""
    ini["Data\\INI\\Locomotor.ini"] = """
Locomotor TstTreads
  Speed = 30.0
End
"""
    ini["Data\\INI\\FXList.ini"] = """
FXList FX_TstFire
  Sound
    Name = TstCannonSnd
  End
  ParticleSystem
    Name = TstPuff
  End
End

FXList FX_TstBoom
  Sound
    Name = TstCannonSnd
  End
End
"""
    ini["Data\\INI\\ParticleSystem.ini"] = """
ParticleSystem TstPuff
  ParticleName = tst_puff.tga
  Lifetime = 10 20
End
"""
    ini["Data\\INI\\CommandButton.ini"] = """
CommandButton Command_TstBuildTank
  Command = UNIT_BUILD
  Object = TstBaseTank
  TextLabel = CONTROLBAR:TstBuildTank
  ButtonImage = TST_Tank
End
"""
    ini["Data\\INI\\CommandSet.ini"] = """
CommandSet TstHQSet
  1 = Command_TstBuildTank
End
"""
    ini["Data\\INI\\Science.ini"] = """
Science SCIENCE_TstA
  PrerequisiteSciences = None
  SciencePurchasePointCost = 1
  DisplayName = SCIENCE:TstA
End
"""
    ini["Data\\INI\\Upgrade.ini"] = """
Upgrade Upgrade_TstArmor
  DisplayName = UPGRADE:TstArmor
  ButtonImage = TST_Tank
End
"""
    ini["Data\\INI\\SoundEffects.ini"] = """
AudioEvent TstCannonSnd
  Sounds = tstboom
  Volume = 80
End

AudioEvent TstSelect
  Sounds = tstboom
End
"""
    ini["Data\\INI\\MappedImages\\HandCreated\\TstImages.ini"] = """
MappedImage TST_HQ
  Texture = tst_ui.tga
  TextureWidth = 256
  TextureHeight = 256
  Coords = Left:0 Top:0 Right:64 Bottom:48
  Status = NONE
End

MappedImage TST_Tank
  Texture = tst_ui.tga
  TextureWidth = 256
  TextureHeight = 256
  Coords = Left:64 Top:0 Right:128 Bottom:48
  Status = NONE
End
"""
    ini["Data\\INI\\PlayerTemplate.ini"] = """
PlayerTemplate FactionObserver
  Side = Observer
  PlayableSide = No
  IsObserver = Yes
  DisplayName = INI:Observer
End

PlayerTemplate FactionTstBase
  Side = TstBase
  BaseSide = TstBase
  PlayableSide = Yes
  DisplayName = INI:TstBase
  StartMoney = 5000
  StartingBuilding = TstBaseHQ
  StartingUnit0 = TstBaseDozer
  PreferredColor = R:10 G:20 B:30
End
"""
    ini["Data\\INI\\AIData.ini"] = """
AIData
  StructureSeconds = 5.0
  SideInfo TstBase
    ResourceGatherersEasy = 1
  End
  SkirmishBuildList TstBase
    Structure TstBaseHQ
      Location = X:0 Y:0
      Rebuilds = 1
    End
  End
End
"""
    return ini


def base_strings():
    return {
        "OBJECT:TstBaseHQ": "Test HQ", "OBJECT:TstBaseTank": "Test Tank", "OBJECT:TstBaseDozer": "Test Dozer",
        "CONTROLBAR:TstBuildTank": "Build tank", "SCIENCE:TstA": "Science A", "UPGRADE:TstArmor": "Armor up",
        "INI:TstBase": "Test Army", "INI:Observer": "Observer",
    }


def make_scb(players):
    """players: {name: [(script name, [(param kind, value)...])]} -> bytes. Teams: one default team per player."""
    out = scbmod.Scb()
    for name, scripts in players.items():
        out.players.append((name, scbmod.Dict()))
        sl = scbmod.ScriptList()
        for sname, actions in scripts:
            s = scbmod.Script()
            s.name = sname
            s.or_conditions = [(1, [scbmod.Call(4, 3, "CONDITION_TRUE", [])])]
            for aname, atype, params in actions:
                ps = []
                for kind, value in params:
                    t = scbmod.PARAM_NAMES.index(kind)
                    if kind in ("INT", "BOOLEAN", "COMPARISON"):
                        ps.append(scbmod.Param(t, None, int(value), 0.0, ""))
                    else:
                        ps.append(scbmod.Param(t, None, 0, 0.0, str(value)))
                s.actions.append(scbmod.Call(2, atype, aname, ps))
            sl.items.append(s)
        out.lists.append(sl)
        t = scbmod.Dict()
        t.set("teamName", "team" + name)
        t.set("teamOwner", name)
        t.set("teamIsSingleton", True)
        out.teams.append(t)
        t2 = scbmod.Dict()
        t2.set("teamName", "Team%sInfantry" % name)
        t2.set("teamOwner", name)
        t2.set("teamUnitType1", "TstBaseTank")
        t2.set("teamProductionCondition", scripts[0][0] if scripts else "")
        t2.set("teamOnCreateScript", "%sAttack" % name)
        out.teams.append(t2)
    return scbmod.write_scb(out)


def build_base(folder):
    """The made-up retail game: a folder with a few archives."""
    os.makedirs(folder, exist_ok=True)
    ini = BigWriter()
    for k, v in base_ini().items():
        ini.add(k, v.encode("latin-1"))
    ini.add("Data\\Scripts\\SkirmishScripts.scb", make_scb({
        "SkirmishTstBase": [("BuildTanks", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")])]),
                            ("TstBaseAttack", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")])])],
        "Civilian": [("CivScript", [])]}))
    ini.write(os.path.join(folder, "INIZH.big"))
    art = BigWriter()
    art.add("Art\\W3D\\TST_TANK.W3D", w3d_model("TST_TANK", "tst_main.tga", hierarchy="TST_SKL"))
    art.add("Art\\W3D\\TST_SKL.W3D", w3d_skeleton("TST_SKL"))
    art.add("Art\\W3D\\TST_WALK.W3D", w3d_anim("TST_WALK", "TST_SKL"))
    art.add("Art\\W3D\\TST_HQ.W3D", w3d_model("TST_HQ", "tst_main.tga"))
    art.add("Art\\W3D\\TST_SHELL.W3D", w3d_model("TST_SHELL", "tst_main.tga"))
    art.write(os.path.join(folder, "W3DZH.big"))
    tex = BigWriter()
    tex.add("Art\\Textures\\tst_main.dds", b"DDS fake texture main")
    tex.add("Art\\Textures\\tst_puff.tga", b"fake tga puff")
    tex.add("Art\\Textures\\tst_ui.tga", b"fake tga ui")
    tex.write(os.path.join(folder, "TexturesZH.big"))
    aud = BigWriter()
    aud.add("Data\\Audio\\Sounds\\tstboom.wav", b"RIFF fake wav")
    aud.write(os.path.join(folder, "AudioZH.big"))
    eng = BigWriter()
    eng.add("Data\\English\\generals.csf", stringsmod.build_csf(base_strings()))
    eng.write(os.path.join(folder, "EnglishZH.big"))


# ---- the mod ---------------------------------------------------------------------------------------------------------
def mod_ini(with_macros=True):
    ini = {}
    ini["Data\\INI\\Object\\ModAlpha.ini"] = """
Object MAlphaHQ
  Side = ModAlpha
  DisplayName = OBJECT:MAlphaHQ
  ButtonImage = MAlpha_HQ
  VoiceSelect = MAlphaVoice
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = malpha_hq
    End
  End
  KindOf = STRUCTURE SELECTABLE COMMANDCENTER
  Body = StructureBody ModuleTag_Body
    MaxHealth = 2000.0
  End
  CommandSet = MAlphaHQSet
  Behavior = CodeNeedingModule ModuleTag_X
    Whatever = 1
  End
End

Object MAlphaTank
  Side = ModAlpha
  DisplayName = OBJECT:MAlphaTank
  ButtonImage = MAlpha_Tank
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = malpha_tank
      Animation = MALPHA_SKL.MALPHA_WALK
    End
    ConditionState = DAMAGED
      Model = malpha_tank
    End
  End
  KindOf = VEHICLE SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 350.0
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY TstCannon
    Weapon = SECONDARY MAlphaRay
  End
  Locomotor = SET_NORMAL TstTreads
  ArmorSet
    Conditions = None
    Armor = TstArmor
  End
  Prerequisites
    Object = MAlphaHQ
  End
  UpgradeCameo1 = Upgrade_TstArmor
  MysteryField = 5
End

ObjectReskin MAlphaTankGold MAlphaTank
  DisplayName = OBJECT:MAlphaTank
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = malpha_hq
    End
  End
End

Object MAlphaDozerOnStock
  Side = ModAlpha
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = tst_tank
    End
  End
End
"""
    ini["Data\\INI\\Weapon\\ModArmies.ini"] = """
Weapon TstCannon
  PrimaryDamage = 55.0
  ProjectileObject = TstProjectile
  FireFX = FX_TstFire
  FireSound = TstCannonSnd
End

Weapon MAlphaRay
  PrimaryDamage = 12.0
  FireSound = MAlphaBoom
  FireFX = FX_MAlphaZap
End
"""
    ini["Data\\INI\\FXList\\ModArmies.ini"] = """
FXList FX_MAlphaZap
  Sound
    Name = MAlphaBoom
  End
  ParticleSystem
    Name = MAlphaSpark
  End
End
"""
    ini["Data\\INI\\ParticleSystem\\ModArmies.ini"] = """
ParticleSystem MAlphaSpark
  ParticleName = malpha_spark.tga
End
"""
    ini["Data\\INI\\CommandButton\\ModArmies.ini"] = """
CommandButton Command_MAlphaTank
  Command = UNIT_BUILD
  Object = MAlphaTank
  TextLabel = CONTROLBAR:MAlphaTank
  ButtonImage = MAlpha_Tank
  Options = NEED_UPGRADE
  Upgrade = Upgrade_TstArmor
End

CommandButton Command_MAlphaGold
  Command = UNIT_BUILD
  Object = MAlphaTankGold
  TextLabel = CONTROLBAR:MAlphaTank
  ButtonImage = MAlpha_Tank
End

CommandButton Command_MAlphaStockTank
  Command = UNIT_BUILD
  Object = TstBaseTank
  TextLabel = CONTROLBAR:TstBuildTank
  ButtonImage = TST_Tank
End
"""
    ini["Data\\INI\\CommandSet\\ModArmies.ini"] = """
CommandSet MAlphaHQSet
  1 = Command_MAlphaTank
  2 = Command_MAlphaStockTank
  3 = Command_MAlphaGold
End
"""
    ini["Data\\INI\\SoundEffects\\ModArmies.ini"] = """
AudioEvent MAlphaBoom
  Sounds = malphaboom malphaboom2
  Volume = 90
End

AudioEvent MAlphaVoice
  Sounds = tstboom
End
"""
    ini["Data\\INI\\MappedImages\\HandCreated\\MAlphaImages.ini"] = """
MappedImage MAlpha_HQ
  Texture = malpha_ui.tga
  TextureWidth = 128
  TextureHeight = 128
  Coords = Left:0 Top:0 Right:64 Bottom:48
  Status = NONE
End

MappedImage MAlpha_Tank
  Texture = malpha_ui.tga
  TextureWidth = 128
  TextureHeight = 128
  Coords = Left:64 Top:0 Right:128 Bottom:48
  Status = NONE
End
"""
    ini["Data\\INI\\PlayerTemplate\\ModArmies.ini"] = """
PlayerTemplate FactionObserver
  Side = Observer
  PlayableSide = No
  IsObserver = Yes
  DisplayName = INI:Observer
End

PlayerTemplate FactionTstBase
  Side = TstBase
  BaseSide = TstBase
  PlayableSide = Yes
  DisplayName = INI:TstBase
  StartMoney = 5000
  StartingBuilding = TstBaseHQ
  StartingUnit0 = TstBaseDozer
  PreferredColor = R:10 G:20 B:30
End

PlayerTemplate FactionModAlpha
  Side = ModAlpha
  BaseSide = TstBase
  PlayableSide = Yes
  DisplayName = INI:ModAlpha
  StartMoney = 7000
  StartingBuilding = MAlphaHQ
  StartingUnit0 = TstBaseDozer
  PreferredColor = R:200 G:10 B:30
End

PlayerTemplate FactionModBeta
  Side = ModBeta
  BaseSide = TstBase
  PlayableSide = Yes
  DisplayName = INI:ModBeta
  StartMoney = 6000
  StartingBuilding = MBetaHQ
  PreferredColor = R:20 G:200 B:30
End

PlayerTemplate FactionModHidden
  Side = ModHidden
  PlayableSide = No
  DisplayName = INI:ModAlpha
End
"""
    macro = """
#define BETA_HP 800.0
#define BETA_COST 450
Object MBetaHQ
  Side = ModBeta
  DisplayName = OBJECT:MBetaHQ
  Draw = W3DModelDraw ModuleTag_Draw
    DefaultConditionState
      Model = mbeta_hq
    End
  End
  Body = StructureBody ModuleTag_Body
    MaxHealth = BETA_HP
  End
  BuildCost = BETA_COST
  // a comment line the stock engine would treat as an unknown field
End
"""
    ini["Data\\INI\\Object\\ModBeta.ini"] = macro
    ini["Data\\INI\\AIData\\ModArmies.ini"] = """
AIData
  SideInfo ModAlpha
    ResourceGatherersEasy = 2
    BaseDefenseStructure1 = MAlphaHQ
  End
  SkirmishBuildList ModAlpha
    Structure MAlphaHQ
      Location = X:0 Y:0
      Rebuilds = 1
    End
  End
  SkirmishBuildList ModBeta
    Structure MBetaHQ
      Location = X:0 Y:0
    End
  End
End
"""
    return ini


def mod_strings():
    s = dict(base_strings())
    s.update({"OBJECT:MAlphaHQ": "Alpha HQ", "OBJECT:MAlphaTank": "Alpha Tank – mk2",
              "CONTROLBAR:MAlphaTank": "Build Alpha tank", "INI:ModAlpha": "Alpha Army", "INI:ModBeta": "Beta Army",
              "OBJECT:MBetaHQ": "Beta HQ"})
    return s


def build_mod_archives(folder, prefix_main="!ModMain.big", prefix_art="zModArt.big"):
    """Write the mod's archives into ``folder`` (inside the game folder or a folder of their own)."""
    os.makedirs(folder, exist_ok=True)
    main = BigWriter()
    for k, v in mod_ini().items():
        main.add(k, v.encode("latin-1"))
    main.add("Data\\Scripts\\SkirmishScripts.scb", make_scb({
        "SkirmishTstBase": [("BuildTanks", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")])]),
                            ("TstBaseAttack", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")])])],
        "SkirmishModAlpha": [("AlphaBuild", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")]),
                                             ("PlayerBuild", 61, [("OBJECT_TYPE", "MAlphaTank"),
                                                                  ("SIDE", "<This Player>")])]),
                             ("ModAlphaAttack", [("UnitTeamHunt", 60, [("TEAM", "<This Team>")])])],
        "Civilian": [("CivScript", [])]}))
    main.add("Data\\English\\generals.csf", stringsmod.build_csf(mod_strings()))
    main.write(os.path.join(folder, prefix_main))
    art = BigWriter()
    art.add("Art\\W3D\\MALPHA_HQ.W3D", w3d_model("MALPHA_HQ", "malpha_hq.tga"))
    art.add("Art\\W3D\\MALPHA_TANK.W3D", w3d_model("MALPHA_TANK", "ZHCmalpha.tga", hierarchy="MALPHA_SKL"))
    art.add("Art\\W3D\\MALPHA_SKL.W3D", w3d_skeleton("MALPHA_SKL"))
    art.add("Art\\W3D\\MALPHA_WALK.W3D", w3d_anim("MALPHA_WALK", "MALPHA_SKL"))
    art.add("Art\\W3D\\MBETA_HQ.W3D", w3d_model("MBETA_HQ", "mbeta_tex.tga"))
    art.add("Art\\Textures\\malpha_hq.tga", b"fake alpha hq tex")
    art.add("Art\\Textures\\ZHCmalpha.tga", b"fake house colour tex")
    art.add("Art\\Textures\\malpha_ui.tga", b"fake alpha ui")
    art.add("Art\\Textures\\malpha_spark.tga", b"fake spark")
    art.add("Art\\Textures\\mbeta_tex.dds", b"fake beta tex")
    art.add("Data\\Audio\\Sounds\\English\\malphaboom.wav", b"RIFF alpha boom english")
    art.add("Data\\Audio\\Sounds\\malphaboom2.wav", b"RIFF alpha boom 2")
    art.write(os.path.join(folder, prefix_art))
