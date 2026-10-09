# zharmy: army package converter

Turns one army (faction) of a Zero Hour mod into a self-contained `.zharmy` package that the web port can load
next to the game data. Format: `docs/ARMY_PACKAGES.md`. Python 3, standard library only. Nothing here contains or
downloads game or mod data; the tool runs on files the player already owns.

```
cd tools
python3 -m zharmy --help                  # or: python3 tools/zharmy/zharmy.py --help
python3 -m zharmy archives  <game folder> [--mod-archives auto|GLOB...]
python3 -m zharmy find      <folder...> <glob>          # which archive / loose file provides matching paths
python3 -m zharmy inspect   <mod...> [--base <ruleset...>] [--mod-archives auto|GLOB...] [--ini-problems]
python3 -m zharmy convert   <mod...> --base <ruleset...> --faction <PlayerTemplate> --tag <TAG> -o out.zharmy
python3 -m zharmy convert-all <mod...> --base <ruleset...> --out-dir <dir> [--tag-prefix CTR]
python3 -m zharmy validate  <pkg...> [--base <ruleset...>] [--mod <mod...> | --mod-archives auto] [--with <other.zharmy>]
tools/zharmy/run_tests.sh                 # all unit tests, about 40 s (20 s of that builds the starter pack)
```

`<mod...>` and `<ruleset...>` are folders and/or `.big` files. A folder contributes its loose files and every `.big`
below it.

## In the browser

The web launcher ("Import armies from a mod…") runs this same code under Pyodide in a Web Worker: pick a mod folder, see the
armies it holds, tick the ones to import; the packages are stored in the browser and used like any other army. The glue is
`webapi.py` (`open_json`, `convert_json`, `retail_json`; JSON in and out), which reads the mod and the ruleset once and converts
several armies from them. It uses `convert`, `layout`, `inspect_mod`'s rules and `validate` as they are; the command line is
unchanged. The mod is mounted read only and read by byte range (nothing is copied into the Python heap). The default for a
mod installed in the game folder is `--mod-archives auto`, and the page's list of "which files belong to the mod" is
`layout.is_retail_archive`. See `docs/ARMY_PACKAGES.md`, "Importing in the browser". The page's build puts `zharmy.zip` (the
`.py` files, not `gen_schema.py` or the tests) next to `pyodide/`.

Measured in headless Chromium on a busy 4 core machine (load average about 17), cold cache: Pyodide and the converter load in
5.7 to 7.8 s (12.4 MB raw, about 5.5 MB with gzip: `pyodide.asm.wasm` 8.6 MB, `python_stdlib.zip` 2.4 MB, `pyodide.asm.js`
1.1 MB, `zharmy.zip` 0.09 MB); the browser keeps them for the next visit. The three-army synthetic mod: reading 0.5 s,
converting 0.05 to 0.33 s per army. The starter content's Ironwood with `--requires none`: reading 0.9 s, converting 0.49 s
(0.3 s with CPython on the same machine). Mods the size of a real total conversion have not been measured here (no retail data
in the sandbox): reading the INI text of the game and the mod is the part that grows, about 15 MB of text.

## How files are layered (what the engine does)

* A loose file beats any archive.
* The archives of one folder are loaded in alphabetical order of their path (case-insensitive) and **the first
  archive that holds a file wins** (`ArchiveFileSystem::loadIntoDirectoryTree`, no overwrite). That is why mod
  archives are often named `!Something.big`: `!` sorts before the retail names. An archive called `zMod.big` would
  lose against any retail archive that has the same path; that is how the game behaves, not a choice of the tool.
* A mod loaded on top of a game (`-mod`) goes the other way: later archives replace earlier ones. `zharmy` uses this
  when you give a mod folder and a different `--base` folder.
* A lone folder (no `--base`) or the same folder as `--base` is treated as a game as installed (first wins).
* A folder with the Zero Hour and the Generals install as sub folders is layered Zero Hour first, Generals second;
  loose files of the Generals folder are not visible to the engine.
* `Data/INI/INIZH.big` (the duplicate some installs carry) is skipped like the engine does.

INI files are loaded in the engine's order: `Data/INI/Default/<Type>.ini`, `Data/INI/<Type>.ini`, then the files
below `Data/INI/<Type>/` (folder first, then sub folders, each sorted). A mod INI file with the same path as a
retail one hides the retail file completely (whole-file shadowing, like the game); mods that only add files below
`Data/INI/<Type>/` keep the retail definitions. The last definition of a name wins.

## Mod installed into the game folder (example: Silent Death)

Typical case: the game folder holds the retail archives (`INIZH.big`, `W3DZH.big`, `EnglishZH.big`, ...) and the
mod's archives next to them. It may also hold the Zero Hour install and the base Generals install as sub folders
(`command and conquer generals zero hour/`, `command and conquer generals/`): the tool detects that, says so, and
layers them like the engine does (Zero Hour's loose files and archives first, then the archives of the base game;
`StdBIGFileSystem::init` loads the working directory, then the Generals install path). Use the same folder as mod and
as ruleset (`--base`). The mod's archives are found **automatically**: every archive whose file name is not a known
retail archive name (`INIZH.big`, `W3DZH.big`, `TexturesZH.big`, `AudioEnglishZH.big`, `Music.big`, `INI.big`, ...,
language variants like `AudioGermanZH.big`) belongs to the mod, e.g. `!00egypatch.big`, `00lsf0118.big`. `auto` is the
default when mod and `--base` are the same folder; the tool prints the archives it picked.

```
cd tools
ZH="D:\Games\Command and Conquer Generals Zero Hour complete"      # holds both install sub folders

# 1. the archives in engine load order, which ones are the mod's (MOD) and which are retail names
python3 -m zharmy archives "$ZH"

# 2. the playable factions (name, side, build list, scripts), the string table language, INI problems
python3 -m zharmy inspect "$ZH" --base "$ZH"
python3 -m zharmy inspect "$ZH" --base "$ZH" --ini-problems       # every INI problem as file:line

# 3. every playable faction, one package each, with a summary table and a report per package
python3 -m zharmy convert-all "$ZH" --base "$ZH" --out-dir ~/armies --tag-prefix SD \
    --mod-name "Silent Death" --report-dir ~/armies/reports --validate

# 4. one faction, own tag and id
python3 -m zharmy convert "$ZH" --base "$ZH" --faction FactionChinaNuke --tag CNUK \
    --id sd.china-nuke --name "Nuke General" -o nuke.zharmy --validate

# 5. when something is not found: which archive or loose file provides a path?
python3 -m zharmy find "$ZH" '*skirmishscripts*'
python3 -m zharmy find "$ZH" 'art/w3d/cwcusac130*'
```

Name the mod's archives yourself with patterns instead of `auto` (matched case-insensitively against the file name
or the path relative to the folder, `*` `?` `[ ]` allowed; in bash put `!` patterns in single quotes):
`--mod-archives '!00egypatch.big' '00lsf0118.big'`. **A pattern that matches no file is an error** that lists the
archives. If mod and `--base` are the same folder and nothing would be selected the tool stops, because everything
would look like retail data; `--mod-archives` with no value means "none" on purpose. Loose mod files can be named too
(`--mod-archives 'Data/INI/*'`).

What happens: the ruleset is the folder *without* the mod's archives; the mod as played is the whole folder under the
engine rule above. A definition counts as the mod's when it is new, or differs from the ruleset's text, or points
(directly or through other definitions) to something that does, or uses a model, texture or sound file the mod
changed. Everything else stays a reference to the ruleset.

**Loose files are part of the mod.** Mods often install loose files (`Data/INI/...`, `Data/<Language>/Generals.csf`,
`Art/...`, `Data/Audio/...`) into the game folder; the engine reads a loose file before any archive. With a mod
installed in the game folder every loose file below `Data/` and `Art/` therefore counts as the mod's, not as retail
data, except a file that is byte-identical to its copy in a retail archive. `archives` and `inspect` print how many
loose files were taken that way and the directories they are in. `--loose ruleset` treats them as retail instead,
`--loose mod` forces the other way. A folder with no archives at all (a ruleset on its own) keeps its loose files as
the ruleset.

**ObjectReskin.** The engine copies the parent of `ObjectReskin <Name> <Parent>` when it reads the line, so the parent
must exist before it. The converter puts the parent (and the parents of reskin chains) into the closure, renames
it, rewrites the reference and writes it before the reskin; `validate` checks that order. A reskin whose parent
exists nowhere is "cannot be converted".

**String table language.** The tool uses the language of the string table the mod brings: English if the mod ships an
English table, else the language of its own table (a Chinese mod without an English table: `Data/Chinese/Generals.csf`),
else the game's English one. The choice is printed (`String tables: chinese (the mod provides only a chinese string
table)`); `--language <name>` overrides. Packages hold single-byte `Strings.str` text, so characters outside Latin-1
(Chinese labels) are replaced by `?` with a warning; the faction's display name in the manifest keeps its characters.

**Where files are looked up.** Like the engine: `Data/<Language>/Art/W3D/` then `Art/W3D/` for models (`FILE.PART`
loads `FILE.w3d`), `Data/<Language>/Art/Textures/` then `Art/Textures/` for textures (a `.tga` request is served by a
`.dds` first; a `.dds` request does not find a `.tga`), `Data/Audio/Sounds/<Language>/` then `Data/Audio/Sounds/` for
sounds with `.wav` appended (`AudioSettings.ini` can change the folders and the extension), tracks and speech by file
name. `SkirmishScripts.scb` is read from `Data/Scripts/SkirmishScripts.scb`: a loose file first, then the archives of
the Zero Hour install, then those of the base game (also decompressed when it carries a `ZL1`..`ZL9` tag).

**Dangling references.** Mods often refer to particle systems, models, sounds, labels or objects that the mod itself
does not have; the engine tolerates that (it stores a null or looks the name up later). A reference that does not
resolve in the mod as played either is a *pre-existing dangling reference*: a warning, counted in its own column of the
summary (`dangling`) and listed in the report (`danglingReferences`), and `validate` accepts the package when it
knows the mod (`--mod <folder>` or `--mod-archives`; `convert-all --validate` knows it). Only three kinds are resolved
while the INI files are read and make the engine throw when missing: sciences, command buttons in command sets and
locomotors in locomotor sets. A faction with such a reference "cannot be converted" (reported, no package written),
and a faction whose `StartingBuilding` / `StartingUnit` does not exist in the mod is skipped as unplayable.

**Output volume.** Warnings are grouped by kind on the console (the first three of each kind, then "and N more");
the full list is in the report JSON (`--report`, `--report-dir`): `warnings` (all), `warningGroups` (by kind),
`danglingReferences`.

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
| `archives` | lists `.big` files in load order (both installs of a combined folder), marks the mod's (default `auto`) and the retail names, and counts the loose files treated as the mod's (`--loose mod|ruleset`) |
| `find` | `find <folder...> <glob>`: which archive or loose file provides matching paths, `->` marks the one the engine uses |
| `inspect` | lists playable factions: template, side, display name, build list, scripts, whether the ruleset has it, unplayable ones; layout, language, INI problem summary (`--ini-problems`: all) |
| `convert` | one faction to one package; `--requires zerohour|starter|none`, `--id`, `--name`, `--version`, `--author`, `--license`, `--mod-name`, `--mod-version`, `--mod-url`, `--language`, `--report file.json`, `--validate` |
| `convert-all` | every playable faction (or `--faction X` repeated); tags `PREFIX1..n` or derived from the names |
| `validate` | the rules of the format document; `--base` for ruleset checks, `--mod` / `--mod-archives` for the mod as played (references it lacks are warnings), `--with` for other loaded packages |

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
layout.py search.py                          game folder layout, retail archive names, mod archive selection;
                                             where the engine looks for models, textures, sounds
ini.py gamedata.py                           INI dialect parser/writer; definitions in engine load order
schema.py schema_data.py gen_schema.py       reference schema (data generated from the engine sources)
refs.py assets.py w3d.py                     finding / rewriting references; file collection; W3D chunks
strings.py scb.py                            CSF/STR tables; skirmish script files
convert.py inspect_mod.py validate.py        the three commands
webapi.py                                    entry points of the in-browser importer (not used by the command line)
package.py                                   ZIP container, manifest, contentHash
tests/                                       unit tests, synthetic game + mod fixtures (written from scratch)
```
