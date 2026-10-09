# zharmy: army package converter

Turns one army (faction) of a Zero Hour mod into a self-contained `.zharmy` package that the web port can load
next to the game data. Format: `docs/ARMY_PACKAGES.md`. Python 3, standard library only. Nothing here contains or
downloads game or mod data; the tool runs on files the player already owns.

```
cd tools
python3 -m zharmy --help                  # or: python3 tools/zharmy/zharmy.py --help
python3 -m zharmy archives  <game folder> [--mod-archives GLOB...]
python3 -m zharmy inspect   <mod...> [--base <ruleset...>] [--mod-archives GLOB...]
python3 -m zharmy convert   <mod...> --base <ruleset...> --faction <PlayerTemplate> --tag <TAG> -o out.zharmy
python3 -m zharmy convert-all <mod...> --base <ruleset...> --out-dir <dir> [--tag-prefix CTR]
python3 -m zharmy validate  <pkg...> [--base <ruleset...>] [--with <other.zharmy>]
tools/zharmy/run_tests.sh                 # all unit tests, about 40 s (20 s of that builds the starter pack)
```

`<mod...>` and `<ruleset...>` are folders and/or `.big` files. A folder contributes its loose files and every `.big`
below it.

## How files are layered (what the engine does)

* A loose file beats any archive.
* The archives of one folder are loaded in alphabetical order of their path (case-insensitive) and **the first
  archive that holds a file wins** (`ArchiveFileSystem::loadIntoDirectoryTree`, no overwrite). That is why mod
  archives are often named `!Something.big`: `!` sorts before the retail names. An archive called `zMod.big` would
  lose against any retail archive that has the same path; that is how the game behaves, not a choice of the tool.
* A mod loaded on top of a game (`-mod`) goes the other way: later archives replace earlier ones. `zharmy` uses this
  when you give a mod folder and a different `--base` folder.
* A lone folder (no `--base`) or the same folder as `--base` is treated as a game as installed (first wins).
* `Data/INI/INIZH.big` (the duplicate some installs carry) is skipped like the engine does.

INI files are loaded in the engine's order: `Data/INI/Default/<Type>.ini`, `Data/INI/<Type>.ini`, then the files
below `Data/INI/<Type>/` (folder first, then sub folders, each sorted). A mod INI file with the same path as a
retail one hides the retail file completely (whole-file shadowing, like the game); mods that only add files below
`Data/INI/<Type>/` keep the retail definitions. The last definition of a name wins.

## Mod installed into the Zero Hour folder

Typical case: `D:\Games\ZeroHour` holds the retail archives (`INIZH.big`, `W3DZH.big`, `EnglishZH.big`, ...) and
the mod's archives next to them. Use the same folder as mod and as ruleset and tell the tool which archives are the
mod's. Patterns are matched case-insensitively against the file name or the path relative to the folder, `*` `?`
`[ ]` allowed.

```
cd tools

# 1. which archives are there, in the order the engine loads them, and which does a pattern select?
python3 -m zharmy archives "D:\Games\ZeroHour" --mod-archives "!Contra*.big" "Contra*.big"

# 2. the playable factions of the mod (name, side, build list and scripts present?)
python3 -m zharmy inspect  "D:\Games\ZeroHour" --base "D:\Games\ZeroHour" --mod-archives "!Contra*.big" "Contra*.big"

# 3. every playable faction, one package each, with a summary table
python3 -m zharmy convert-all "D:\Games\ZeroHour" --base "D:\Games\ZeroHour" ^
    --mod-archives "!Contra*.big" "Contra*.big" --out-dir "D:\armies" --tag-prefix CTR ^
    --mod-name Contra --mod-version "009 Final" --report-dir "D:\armies\reports" --validate

# 4. one faction, own tag and id
python3 -m zharmy convert "D:\Games\ZeroHour" --base "D:\Games\ZeroHour" --mod-archives "!Contra*.big" ^
    --faction FactionChinaNuke --tag CNUK --id contra.china-nuke --name "Nuke General" -o nuke.zharmy --validate
```

(`^` continues a line in the Windows command prompt, use `\` in a Unix shell. In bash put the `!` patterns in
single quotes: `'!Contra*.big'`.)

What happens: the ruleset is the folder *without* the archives that match; the mod as played is the whole folder
under the engine rule above. A definition counts as the mod's when it is new, or differs from the ruleset's text,
or points (directly or through other definitions) to something that does, or uses a model, texture or sound file
the mod changed. Everything else stays a reference to the ruleset. If the mod folder and `--base` are the same and
`--mod-archives` is missing the tool says so: everything would look like retail.

A mod that is a separate folder (not installed into the game):

```
python3 -m zharmy convert-all  "D:\Mods\Contra" --base "D:\Games\ZeroHour" --out-dir out --tag-prefix CTR
```

Self-contained packages (copy everything, rely on no ruleset): `--requires none` (no `--base` needed).

Output while it runs goes to stderr with a time stamp; the summary table (faction, tag, package, size in MB,
number of objects / weapons / models / textures / sounds copied, warnings) and the per-package reports go to stdout
or `--report-dir`. Roughly 15 MB of INI text is parsed in under ten seconds; archives are read entry by entry (only
their directories are held in memory, file data is read when a package needs it).

## Commands

| command | does |
| --- | --- |
| `archives` | lists `.big` files in load order, optionally marking the mod's |
| `inspect` | lists playable factions: template, side, display name, build list, scripts, whether the ruleset has it |
| `convert` | one faction to one package; `--requires zerohour|starter|none`, `--id`, `--name`, `--version`, `--author`, `--license`, `--mod-name`, `--mod-version`, `--mod-url`, `--language`, `--report file.json`, `--validate` |
| `convert-all` | every playable faction (or `--faction X` repeated); tags `PREFIX1..n` or derived from the names |
| `validate` | the rules of the format document; `--base` for ruleset checks, `--with` for other loaded packages |

## What a conversion does

1. Loads the INI definitions of the mod (as played) and of the ruleset, plus string tables.
2. Walks every reference from the faction's `PlayerTemplate` (schema below).
3. Keeps unchanged ruleset definitions as references (`--requires` not `none`); copies the rest as `<TAG>_<name>`.
   The template and all objects of the faction's side are always copied (the side becomes `<TAG>_<Side>`);
   anything else that pointed to a changed definition is copied with it.
4. Collects model, animation, texture and sound files through the layered file system. W3D files are parsed and every
   name they define (render objects, hierarchies, animations, containers, emitters) is replaced by `<TAG><base 36>`
   (max 15 characters); references inside and outside (INI `Model`, `Animation`, textures) are rewritten. The
   mapping goes to the report (`w3dNames`). The part after the dot of a mesh name (`HOUSECOLOR..`, `TREADS..`) is kept
   because the engine reads its first letters. Textures whose name starts with `ZHC` (house-colour textures) are written
as `ZHC<TAG>...` so the team colour keeps working (rule 4 accepts that prefix; `convert --no-zhc-names` uses plain
`<TAG>...` names instead and the texture loses its team colour, with a warning).
5. Copies the labels the army uses from the mod's string table to `Army/Strings.str` as `<TAG>:<label>`.
6. Writes `SideInfo`/`SkirmishBuildList` of the side to `AIData.ini` (renamed) and the side's script list and teams
   from `SkirmishScripts.scb` to `Army/Scripts/Skirmish.scb` (player `Skirmish<TAG>_<Side>`, scripts, teams, counters
   and flags prefixed with `<TAG>_`, object / upgrade / science / audio names mapped).
7. Writes `manifest.json` (with `contentHash`) and the report.

Entries are written in lower case, sorted, with fixed time stamps: converting twice gives identical bytes.

## Reference schema

`schema_data.py` is generated from the engine's `FieldParse` tables by `gen_schema.py` (3500 field names, 223
module names, the 62 block types of `INI.cpp`). `schema.py` maps a field to what it names: the parse function
decides (`parseFXList` -> FXList, `parseWeaponTemplate` -> Weapon, `parseMappedImage` -> MappedImage, 
`parseAudioEventRTS` -> AudioEvent, `parseScience[Vector]`, `parseThingTemplate`, `parseUpgradeTemplate`,
`parseArmorTemplate`, `parseDamageFX`, `parseSpecialPowerTemplate`, command set slots -> CommandButton,
`parseAndTranslateLabel` -> string label, the OCL, particle system and damage FX variants, payload / rider /
roster lists), plus about 150 plain-string fields listed by hand (`NAME_KINDS`) and block-specific ones
(`SCOPED`: nugget `Name` of FXList Sound / ParticleSystem, `Prerequisites`, `ConditionState` `Model` and `Animation`,
audio `Sounds` / `Filename`, `MappedImage` `Texture`, `PlayerTemplate`, AI side blocks ...).

Covered block types: Object (and ObjectReskin), Weapon, Locomotor, Armor, DamageFX, CommandButton, CommandSet, FXList,
ObjectCreationList, ParticleSystem, Upgrade, Science, SpecialPower, AudioEvent / MusicTrack / DialogEvent,
MappedImage, PlayerTemplate, CrateData, AIData (`SideInfo`, `SkirmishBuildList`, `Structure`, `SkillSet`).
Assets: W3D models and animations, textures, sounds, speech, music. Unknown fields are kept as written and listed
in the report; modules the engine does not have (`ModuleFactory`) are removed from the copy and listed.

## Not carried over (reported)

Global data an army cannot add: EVA sounds per side, control bar schemes, rank / science store changes, `GameData`,
mouse and UI files, maps. Code-dependent features: modules that are not in the engine (removed), fields the engine
does not know (kept, reported). `#include`, `#define` and `//` lines are not understood by the stock engine; the
converter expands them (extension, reported). The engine has no `ChildObject`; `AddModule` / `ReplaceModule` /
`RemoveModule` / `InheritableModule` blocks are parsed and kept. A reskin takes the side of its parent: if the
parent is not on the faction's side the report warns. Only `SkirmishScripts.scb`-style lists named
`Skirmish<Side>` are looked for. Known limit: if a skeleton or other W3D file that a model uses changed but the model
itself did not, the model counts as unchanged.

## Files

```
__init__.py __main__.py zharmy.py cli.py     entry points
bigfile.py vfs.py                            BIGF/BIG4 reader + writer; layered case-insensitive file system
ini.py gamedata.py                           INI dialect parser/writer; definitions in engine load order
schema.py schema_data.py gen_schema.py       reference schema (data generated from the engine sources)
refs.py assets.py w3d.py                     finding / rewriting references; file collection; W3D chunks
strings.py scb.py                            CSF/STR tables; skirmish script files
convert.py inspect_mod.py validate.py        the three commands
package.py                                   ZIP container, manifest, contentHash
tests/                                       unit tests, synthetic game + mod fixtures (written from scratch)
```
