"""Reference schema: which INI fields name other definitions or asset files.

The base is mechanical. ``schema_data.FIELD_PARSERS`` (generated from the engine's ``FieldParse`` tables by
``gen_schema.py``) says which parse function reads each field. A field read by ``INI::parseFXList`` names an
FXList, one read by ``INI::parseWeaponTemplate`` names a Weapon, and so on (``FN_KINDS``). Fields that the engine
reads as plain strings (``INI::parseAsciiString`` ...) are listed by hand below (``NAME_KINDS`` for names that
mean the same wherever they appear, ``SCOPED`` for names whose meaning depends on the block they are in).

Kinds of references
-------------------
Definitions (copied, renamed, or kept as references to the ruleset):
    Object Weapon Locomotor Armor DamageFX CommandButton CommandSet FXList OCL ParticleSystem Upgrade Science
    SpecialPower Audio MappedImage Crate
Assets (files collected through the virtual file system, renamed with the tag):
    Model  (``Art/W3D/<name>.w3d``)        Anim  (``HIERARCHY.ANIMATION``: file ``Art/W3D/<ANIMATION>.w3d``)
    Texture (``Art/Textures/<name>`` .dds/.tga)
    AudioFile (``Data/Audio/Sounds/<name>.wav``)  TrackFile (``Data/Audio/Tracks/<name>``)
    SpeechFile (``Data/Audio/Speech/<name>``)
Other:
    Label (a key of the string table)   Side (a side name)

Scopes
------
A field is looked up as ``(scope, field)`` for each scope of its position, innermost first, then by name only.
Scopes are: the nugget (``FXList.Sound``), ``Prerequisites``, ``ConditionState``, the module type
(``W3DTreeDraw``), the top level block type (``Object``), and ``*``.
"""

from . import schema_data

DEFINITION_KINDS = ("Object", "Weapon", "Locomotor", "Armor", "DamageFX", "CommandButton", "CommandSet", "FXList",
                    "OCL", "ParticleSystem", "Upgrade", "Science", "SpecialPower", "Audio", "MappedImage", "Crate")
ASSET_KINDS = ("Model", "Anim", "Texture", "AudioFile", "TrackFile", "SpeechFile")

FN_KINDS = {
    # FXList
    "INI::parseFXList": "FXList", "parseFX": "FXList", "parseMajorFXList": "FXList", "parseMinorFXList": "FXList",
    "parseAllVetLevelsFXList": "FXList", "parsePerVetLevelFXList": "FXList", "parseTransitionToFX": "FXList",
    "TransitionDamageFXModuleData::parseFXList": "FXList", "BoneFXUpdateModuleData::parseFXList": "FXList",
    "parseAngleFX": "FXList",
    # ObjectCreationList
    "INI::parseObjectCreationList": "OCL", "parseOCL": "OCL", "parseOCLUpgradePair": ("Upgrade", "OCL"),
    "parseAllVetLevelsAsciiString": "OCL", "parsePerVetLevelAsciiString": "OCL", "parseTransitionToOCL": "OCL",
    "parseFactionObjectCreationList": ("Side", "OCL"),
    "TransitionDamageFXModuleData::parseObjectCreationList": "OCL",
    "BoneFXUpdateModuleData::parseObjectCreationList": "OCL",
    # particle systems
    "INI::parseParticleSystemTemplate": "ParticleSystem", "parseAllVetLevelsPSys": "ParticleSystem",
    "parsePerVetLevelPSys": "ParticleSystem",
    "TransitionDamageFXModuleData::parseParticleSystem": "ParticleSystem",
    "BoneFXUpdateModuleData::parseParticleSystem": "ParticleSystem",
    # the rest
    "INI::parseWeaponTemplate": "Weapon", "parseWeapon": "Weapon", "WeaponTemplateSet::parseWeapon": "Weapon",
    "INI::parseMappedImage": "MappedImage",
    "INI::parseAudioEventRTS": "Audio", "INI::parseDynamicAudioEventRTS": "Audio",
    "INI::parseSpecialPowerTemplate": "SpecialPower",
    "INI::parseScience": "Science", "INI::parseScienceVector": "Science", "AI::parseScience": "Science",
    "INI::parseThingTemplate": "Object", "INI::parseUpgradeTemplate": "Upgrade", "INI::parseArmorTemplate": "Armor",
    "INI::parseDamageFX": "DamageFX", "CommandSet::parseCommandButton": "CommandButton",
    "CreateCrateDieModuleData::parseCrateData": "Crate", "INI::parseAndTranslateLabel": "Label",
    "parseInitialPayload": "Object", "parsePayload": "Object", "parseInitialRoster": "Object",
    "parseRiderInfo": "Object", "parseAppendQuantityModifier": "Object",
    "parseUpgradePair": "Upgrade", "parseCashHackUpgradePair": "Upgrade", "parseBountyUpgradePair": "Upgrade",
    "AIUpdateModuleData::parseLocomotorSet": "Locomotor",
    "CrateTemplate::parseCrateCreationEntry": "Object",
}

# names read as plain strings, by field name (same meaning wherever they appear)
NAME_KINDS = {
    # objects
    "FlareTemplateName": "Object", "PutInContainer": "Object", "SpecialObject": "Object", "ParachuteName": "Object",
    "UnitName": "Object", "BaseDefenseStructure1": "Object", "Transport": "Object", "ProjectileObject": "Object",
    "ProjectileStreamName": "Object", "LaserName": "Object", "ReferenceObject": "Object",
    "DetonationObject": "Object", "ReplaceObject": "Object", "PayloadTemplate": "Object",
    "WorkerObjectName": "Object", "MineName": "Object", "UpgradedMineName": "Object",
    "DamagePulseRemnantObjectName": "Object", "VisionObjectName": "Object", "GattlingTemplateName": "Object",
    "GunshipTemplateName": "Object", "StumpName": "Object", "BladeObjectName": "Object",
    "FinalRubbleObject": "Object", "VisiblePayloadTemplateName": "Object", "RopeName": "Object",
    "HoleName": "Object", "StartingBuilding": "Object", "BeaconName": "Object",
    "StartingUnit0": "Object", "StartingUnit1": "Object", "StartingUnit2": "Object", "StartingUnit3": "Object",
    "StartingUnit4": "Object", "StartingUnit5": "Object", "StartingUnit6": "Object", "StartingUnit7": "Object",
    "StartingUnit8": "Object", "StartingUnit9": "Object",
    "CarriageTemplateName": "Object", "PayloadTemplateName": "Object", "SpawnTemplateName": "Object",
    "BuildVariations": "Object", "ObjectNames": "Object",
    "TowerObjectNameFromLeft": "Object", "TowerObjectNameFromRight": "Object", "TowerObjectNameToLeft": "Object",
    "TowerObjectNameToRight": "Object", "ScaffoldObjectName": "Object", "ScaffoldSupportObjectName": "Object",
    "ConnectorMediumLaserName": "Object", "ConnectorIntenseLaserName": "Object", "ParticleBeamLaserName": "Object",
    "LaserFromAssisted": "Object", "LaserToTarget": "Object",
    # weapons
    "CrushingWeaponName": "Weapon",
    # upgrades
    "UpgradeToRemove": "Upgrade", "UpgradeToGrant": "Upgrade", "TriggerAlt": "Upgrade", "UpgradeRequired": "Upgrade",
    "UpgradedTriggeredBy": "Upgrade", "ConflictsWith": "Upgrade", "RemovesUpgrades": "Upgrade",
    "TriggeredBy": "Upgrade", "UpgradeCameo1": "Upgrade", "UpgradeCameo2": "Upgrade", "UpgradeCameo3": "Upgrade",
    "UpgradeCameo4": "Upgrade", "UpgradeCameo5": "Upgrade", "RequiresAllTriggers": None,
    # sciences / command sets
    "GrantScience": "Science",
    "CommandSet": "CommandSet", "CommandSetAlt": "CommandSet", "PurchaseScienceCommandSetRank1": "CommandSet",
    "PurchaseScienceCommandSetRank3": "CommandSet", "PurchaseScienceCommandSetRank8": "CommandSet",
    "SpecialPowerShortcutCommandSet": "CommandSet",
    # particle systems
    "RepairWeldingSys": "ParticleSystem", "OuterNodesLightFlareParticleSystem": "ParticleSystem",
    "OuterNodesMediumFlareParticleSystem": "ParticleSystem", "OuterNodesIntenseFlareParticleSystem": "ParticleSystem",
    "ConnectorMediumFlare": "ParticleSystem", "ConnectorIntenseFlare": "ParticleSystem",
    "LaserBaseLightFlareParticleSystemName": "ParticleSystem",
    "LaserBaseMediumFlareParticleSystemName": "ParticleSystem",
    "LaserBaseIntenseFlareParticleSystemName": "ParticleSystem",
    "MuzzleParticleSystem": "ParticleSystem", "TargetParticleSystem": "ParticleSystem",
    "RotorWashParticleSystem": "ParticleSystem", "Dust": "ParticleSystem", "DirtSpray": "ParticleSystem",
    "PowerslideSpray": "ParticleSystem", "TreadDebrisLeft": "ParticleSystem", "TreadDebrisRight": "ParticleSystem",
    "SlaveSystem": "ParticleSystem", "PerParticleAttachedSystem": "ParticleSystem",
    # audio
    "BurningSoundName": "Audio", "PoweringUpSoundLoop": "Audio", "UnpackToIdleSoundLoop": "Audio",
    "FiringToPackSoundLoop": "Audio", "GroundAnnihilationSoundLoop": "Audio",
    "BombardmentPlanUnpackSoundName": "Audio", "BombardmentPlanPackSoundName": "Audio",
    "BombardmentAnnouncementName": "Audio", "SearchAndDestroyPlanUnpackSoundName": "Audio",
    "SearchAndDestroyPlanIdleLoopSoundName": "Audio", "SearchAndDestroyPlanPackSoundName": "Audio",
    "SearchAndDestroyAnnouncementName": "Audio", "HoldTheLinePlanUnpackSoundName": "Audio",
    "HoldTheLinePlanPackSoundName": "Audio", "HoldTheLineAnnouncementName": "Audio",
    "LoadScreenMusic": "Audio", "ScoreScreenMusic": "Audio", "FadeSound": "Audio",
    "DamagedToSound": "Audio", "RepairedToSound": "Audio",
    # images
    "ButtonImage": "MappedImage", "SelectPortrait": "MappedImage", "ScoreScreenImage": "MappedImage",
    "LoadScreenImage": "MappedImage", "HeadWaterMark": "MappedImage", "FlagWaterMark": "MappedImage",
    "EnabledImage": "MappedImage", "DisabledImage": "MappedImage", "HiliteImage": "MappedImage",
    "PushedImage": "MappedImage", "SideIconImage": "MappedImage", "GeneralImage": "MappedImage",
    "MedallionRegular": "MappedImage", "MedallionHilite": "MappedImage", "MedallionSelect": "MappedImage",
    "InventoryImageEnabled": "MappedImage", "InventoryImageDisabled": "MappedImage",
    "InventoryImageHilite": "MappedImage", "InventoryImagePushed": "MappedImage",
    # labels
    "TextLabel": "Label", "DescriptLabel": "Label", "PurchasedLabel": "Label", "ConflictingLabel": "Label",
    "ArmyTooltip": "Label", "Features": "Label", "BombardmentMessageLabel": "Label",
    "SearchAndDestroyMessageLabel": "Label", "HoldTheLineMessageLabel": "Label",
    # sides
    "Side": "Side",
    # assets
    "ShadowTexture": "Texture", "TrackMarks": "Texture", "ParticleName": "Texture",
    "ModelNames": "Model", "AttachToBoneInAnotherModule": None,
}
NAME_KINDS = {k: v for k, v in NAME_KINDS.items() if v}

SCOPED = {
    "Prerequisites": {"Object": "Object", "Science": "Science"},
    "ConditionState": {"Model": "Model", "Animation": "Anim", "IdleAnimation": "Anim"},
    "W3DPropDraw": {"ModelName": "Model"},
    "W3DTreeDraw": {"ModelName": "Model", "TextureName": "Texture"},
    "W3DLaserDraw": {"Texture": "Texture"},
    "W3DProjectileStreamDraw": {"Texture": "Texture"},
    "Decal": {"Texture": "Texture"},
    "FXList.Sound": {"Name": "Audio"},
    "FXList.ParticleSystem": {"Name": "ParticleSystem"},
    "FXList.RayEffect": {"Name": "Object"},
    "FXList.Tracer": {"TracerName": "Object"},
    "OCL.CreateDebris": {"ModelNames": "Model", "ObjectNames": "Object"},
    "OCL.CreateObject": {"ObjectNames": "Object"},
    "OCL.DeliverPayload": {"Payload": "Object"},
    "OCL.Attack": {},
    "Upgrade": {"DisplayName": "Label", "ButtonImage": "MappedImage"},
    "MappedImage": {"Texture": "Texture"},
    "ParticleSystem": {"ParticleName": "Texture"},
    "EvaEvent": {"Sounds": "Audio", "Side": "Side"},
    "AudioEvent": {"Sounds": "AudioFile", "Attack": "AudioFile", "Decay": "AudioFile", "SoundsMorning": "AudioFile",
                   "SoundsEvening": "AudioFile", "SoundsNight": "AudioFile"},
    "MusicTrack": {"Filename": "TrackFile"},
    "DialogEvent": {"Filename": "SpeechFile", "Sounds": "SpeechFile"},
    "PlayerTemplate": {"Side": "Side"},
    "SkillSet": {"Science": "Science"},
    "SideInfo": {"BaseDefenseStructure1": "Object"},
    "Structure": {},
}

# fields that are single tokens that need not exist (keywords), never reported when unresolved
SOFT_WORDS = frozenset(["none", "nosound", "(none)"])


def kinds_for(scopes, field):
    """Kinds of the references in ``field`` given ``scopes`` (innermost first). Returns a tuple, maybe empty."""
    for scope in scopes:
        table = SCOPED.get(scope)
        if table is not None and field in table:
            return _tuple(table[field])
    if field in NAME_KINDS:
        return _tuple(NAME_KINDS[field])
    parsers = schema_data.FIELD_PARSERS.get(field)
    if parsers:
        out = []
        for fn in parsers:
            k = FN_KINDS.get(fn)
            if k:
                for kk in _tuple(k):
                    if kk not in out:
                        out.append(kk)
        # a name read by several different typed parsers is ambiguous: keep all
        return tuple(out)
    return ()


def _tuple(k):
    return k if isinstance(k, tuple) else (k,)


def scopes_for(chain):
    """Scopes of a field whose ancestors are ``chain`` (top level node first), innermost first."""
    out = []
    top = chain[0].name
    for node in reversed(chain[1:]):
        n = node.name
        if top == "FXList" and n in ("Sound", "RayEffect", "Tracer", "LightPulse", "ViewShake", "TerrainScorch",
                                     "ParticleSystem", "FXListAtBonePos"):
            out.append("FXList." + n)
        elif top == "ObjectCreationList" and n in ("CreateObject", "CreateDebris", "ApplyRandomForce",
                                                   "DeliverPayload", "FireWeapon", "Attack"):
            out.append("OCL." + n)
        elif n in ("DefaultConditionState", "ConditionState", "TransitionState"):
            out.append("ConditionState")
        elif n in ("DeliveryDecal", "AttackAreaDecal", "TargetingReticleDecal", "GridDecalTemplate"):
            out.append("Decal")
        elif n in ("Behavior", "Body", "Draw", "ClientUpdate"):
            toks = node.args.split()
            if toks:
                out.append(toks[0])
        elif n in ("SkillSet1", "SkillSet2", "SkillSet3", "SkillSet4", "SkillSet5"):
            out.append("SkillSet")
        else:
            out.append(n)
    out.append(top)
    out.append("*")
    return out


def is_known_field(name):
    return name in schema_data.FIELD_PARSERS


def is_known_module(name):
    return name in schema_data.MODULE_NAMES


def field_tokens(args):
    return args.split()
