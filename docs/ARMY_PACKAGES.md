# Army packages (`.zharmy`)

Status: design v1, in progress on the `army-packages` work branch.

Players add extra armies (factions) to Zero Hour without replacing the game:

1. A **converter** (`tools/zharmy`) turns an army from a mod the player owns into a
   self-contained package file, `<something>.zharmy`.
2. The player keeps their packages in one folder.
3. The **launcher** lets them pick that folder (optional), lists the armies it finds and
   which can be used with the current game data, and passes the checked ones to the engine.
4. The **engine** loads each package on top of the game data (the "ruleset"); its
   factions appear in the skirmish faction list next to the standard ones.

One engine build serves every army; nothing is compiled per mod. We never host or
download third-party files: conversion runs on the player's own copy of a mod.

## Terms

- **Ruleset**: the base game data a match runs on (global rules, terrain, crates, stock
  factions). v1 knows two: `zerohour` (the retail Zero Hour data the player picked) and
  `starter` (our free starter pack). A package lists the rulesets it works with.
- **Package**: one `.zharmy` file. It adds one or more factions (usually one).
- **Tag**: a short, unique prefix owned by a package (2-6 characters, `[A-Z][A-Z0-9]{1,5}`),
  e.g. `CNUK`. Every name a package adds starts with `<TAG>_`.

## Container

A `.zharmy` file is a ZIP archive (entries stored or deflated, no encryption, no
ZIP64 needed, UTF-8 names, `/` separators, no absolute paths or `..`). Readers
must ignore unknown entries. Entry names are matched case-insensitively, like game files.

```
manifest.json                 required, see below
Army/INI/<Type>.ini           the definitions the package adds (see "Definitions")
Army/Scripts/Skirmish.scb     optional: skirmish AI scripts for the package's sides
Army/Strings.str              optional: string table (Generals .str format)
Art/W3D/*.w3d                 models, same layout as the game
Art/Textures/*.dds|*.tga      textures
Data/Audio/Sounds/*.wav|*.mp3 sounds (and Data/Audio/Speech/..., Data/Audio/Tracks/...)
LICENSE*, README*             optional, shown by the launcher
```

Art and audio use the game's own directory layout, so the engine can mount the
package like an additional `.big` archive. A package must not contain a file whose
path also exists in the ruleset or in another loaded package (no shadowing), and
must not contain `Data/INI/...`, `Data/<Language>/...` or `Maps/...` entries.

Engine rules for the entries (the engine refuses a package that breaks one):

- Allowed: `manifest.json`; the `Army/` files above (`Army/INI/` only with the file names listed
  under "Definitions"); `LICENSE*` and `README*` in the root; assets under `Art/` and `Data/Audio/`.
  Everything else is refused. A second `Army/...` or `Art/...` entry that differs only in case
  is a duplicate and refuses the archive.
- The no-shadowing rule covers the asset entries (`Art/`, `Data/Audio/`). `manifest.json`,
  `LICENSE*`, `README*` and everything under `Army/` belong to the package alone: the engine reads
  them from the archive directly and never adds them to the game's file tree, so two packages may
  both carry `Army/INI/Object.ini`.
- The file name of every asset (the last path component) starts with the tag, ignoring case.

## manifest.json

```json
{
  "format": 1,
  "id": "contra.china-nuke",
  "tag": "CNUK",
  "name": "Nuke General",
  "version": "1.0.0",
  "description": "Converted from Contra 009 Final.",
  "authors": ["..."],
  "license": "As stated by the mod's authors; for personal use.",
  "source": { "mod": "Contra", "modVersion": "009 Final", "url": "https://..." },
  "requires": ["zerohour"],
  "factions": [
    {
      "playerTemplate": "CNUK_FactionChinaNuke",
      "side": "CNUK_ChinaNuke",
      "displayName": "Nuke General",
      "ai": true
    }
  ],
  "contentHash": "sha256:<hex>",
  "converter": { "tool": "zharmy", "version": "1.0.0", "warnings": [] }
}
```

- `format`: 1. Engines refuse every other number (a higher one means a newer package format).
- `id`: `[a-z0-9][a-z0-9.-]{2,63}`, globally unique by convention (`<mod>.<army>`).
- `requires`: required. Rulesets this package works with (any one of them); `[]` = works with
  any ruleset (fully self-contained). The engine and the launcher refuse a package
  whose `requires` does not include the current ruleset.
- `factions[]`: `playerTemplate` and `side` both start with `<TAG>_` and use only letters,
  digits and `_`; `displayName` is 1 to 64 characters (UTF-8); `ai` defaults to false.
  `playerTemplate` and `side` are unique inside the package (one side per faction).
  At most 8 factions per package and 8 computer-playable (`ai: true`) factions in a run.
  The engine adds the string `SIDE:<side>` (the name in the skirmish list) with `displayName` as
  its text when `Army/Strings.str` has no such label.
- `factions[].ai`: the package provides a skirmish build list and scripts for this
  faction, so the computer can play it. `false` = humans only; the launcher says so
  and the engine does not offer it to AI slots.
- `contentHash`: required, `sha256:` and 64 hex digits. SHA-256 over all entries except
  `manifest.json`, sorted by lower-cased entry name (byte order), each hashed as
  `name \0 size(le64) data` where `name` is the lower-cased entry name and `size` is the
  uncompressed size. Used to detect edits and, later, to check that all multiplayer peers have
  the same package. The engine checks it on every load; it also accepts the hash computed with
  the entry names as stored (not lower-cased), because both readings of this sentence exist.

## Definitions

`Army/INI/` holds ordinary Zero Hour INI definitions, one file per block type, named like the
game's files. The files are flattened: no `#include`, no `#define` (the converter expands them).

| File | Block type it may contain |
| --- | --- |
| `Science.ini` | `Science` |
| `SoundEffects.ini`, `Voice.ini` | `AudioEvent` |
| `Speech.ini` | `DialogEvent` |
| `Music.ini` | `MusicTrack` |
| `PlayerTemplate.ini` | `PlayerTemplate` |
| `ParticleSystem.ini` | `ParticleSystem` |
| `FXList.ini` | `FXList` |
| `Weapon.ini` | `Weapon` |
| `ObjectCreationList.ini` | `ObjectCreationList` |
| `Locomotor.ini` | `Locomotor` |
| `SpecialPower.ini` | `SpecialPower` |
| `DamageFX.ini` | `DamageFX` |
| `Armor.ini` | `Armor` |
| `Object.ini` | `Object` |
| `Upgrade.ini` | `Upgrade` |
| `MappedImages.ini` | `MappedImage` |
| `CommandButton.ini` | `CommandButton` |
| `CommandSet.ini` | `CommandSet` |
| `Crate.ini` | `CrateData` |
| `AIData.ini` | `AIData` (special, see rule 5) |

Any other file name (including `Eva.ini` and `Rank.ini`: those tables are global and cannot be
extended) refuses the package. Another top-level block type in a file refuses the package too.
The engine loads the files in the order of this table, which follows the order in which it loads
the same kinds of data from the game files.

Rules (the engine checks them and refuses a package that breaks one):

1. **Only additions.** Every top-level definition name the package adds starts with
   `<TAG>_` (exactly this spelling) and has something after it. A package never redefines or
   modifies a definition of the ruleset or of another package: a name that exists already
   (case-insensitive for the stores that are case-insensitive) refuses the package, also when it
   starts with the prefix. A name may use any characters except white space and `=` `,` `;`
   (`TK-XLocomotor` is fine). `AIData` is the one exception, see below.
2. **References** may point to the package's own names, or to names the ruleset is
   guaranteed to have (only if `requires` is non-empty). The engine does not check references
   itself: the INI reader throws on the ones it resolves while reading (for example a science
   that does not exist), and `zharmy validate` is the place that checks all of them. A package
   that fails this way stops the game (see "Loading"). Only three kinds of reference are
   resolved while reading, and so must exist: a science (`INI::scanScience`), a command button in
   a command set and a locomotor in a locomotor set. Every other kind (object, weapon, armor,
   FX list, OCL, particle system, upgrade, special power, mapped image, audio event, damage FX,
   string label, model, texture, sound file) is stored as a null or looked up later and is
   tolerated when missing, so mods often carry dead references. `zharmy validate` reports a
   reference that is missing in the ruleset *and* in the mod as played (`--mod` /
   `--mod-archives`) as a "pre-existing dangling reference" warning, not an error.
3. **Sides**: each faction has its own side name `<TAG>_<Name>`; its objects use that
   side. A package does not add objects to other sides: every `Side =` of an `Object` and the
   `Side` of the `PlayerTemplate` must be one of the manifest's sides. Every `PlayerTemplate`
   the package defines is listed in the manifest, and every faction in the manifest has its
   template there (with `PlayableSide = Yes` and a `StartingBuilding`).
4. **Assets**: every file name the package adds (models, textures, sounds) starts
   with the tag, or, for house-colour textures, with `ZHC` followed by the tag (the engine
   takes team colour from the `ZHC` file name prefix) (case-insensitive); W3D-internal names (mesh, hierarchy, container,
   texture references, max 15 characters + NUL) are rewritten to match by the converter.
5. **AIData**: `Army/INI/AIData.ini` contains `AIData` blocks with only
   `SideInfo <side>` and `SkirmishBuildList <side>` entries, where `<side>` is a side of a
   manifest faction; they are merged into the ruleset's AIData. Any other setting inside
   `AIData` (the global AI numbers) refuses the package. A faction with `ai: true` needs a
   `SkirmishBuildList` for its side.
6. **Strings**: labels in `Army/Strings.str` start with `<TAG>_`, `<TAG>:` or `SIDE:<TAG>_`
   (case-insensitive) and do not exist yet. Strings are read before the definitions, so a
   `DisplayName = <TAG>:...` in a definition finds its text.
7. **Skirmish scripts**: `Army/Scripts/Skirmish.scb` has the layout of the game's
   `SkirmishScripts.scb` (chunks `PlayerScriptsList`, `ScriptsPlayers`, `ScriptTeams`). The
   player of a computer-playable faction is named `Skirmish<side>` (for the side
   `CNUK_ChinaNuke`: `SkirmishCNUK_ChinaNuke`); the file may only contain scripts and teams for
   those players (a script for any other player, or a team owned by another player, refuses the
   package). Script, team and counter names should start with the tag too, so they cannot meet
   those of the game's scripts. The default team of the player (`team<player name>`) is added
   by the engine when the file has none.
8. **Limits**: at most 512 upgrades exist in all (`UPGRADE_MAX_COUNT`; a package that would go
   over it is refused), at most 8 computer-playable army factions per run, up to 32 package
   files, a text entry (INI, strings, scripts) is at most 32 MB.

## Loading (engine)

- Command line: `-army <path to .zharmy>`, repeatable. The launcher passes one per
  checked package, as two arguments with an absolute path of the engine's file system
  (the web launcher: `-army /armies/<name>.zharmy`, names may sit in sub folders). The engine opens
  the path as given through the normal file system; nothing assumes the game folder. The path is
  written to the report unchanged. Load order is by `id` (not command-line order), so template
  numbering is deterministic: the new `PlayerTemplate`s are appended to the game's, so
  their numbers (as saved in skirmish settings) depend on the set of packages that load.
- The engine does this once at start, after the ruleset's INI data is loaded and before the
  subsystems post-process it (`GameEngine::init`, `ArmyPackages::load`). Per package, in order:
  open the archive and read the manifest; check `requires`, the entries, the content hash,
  every definition file (block names, prefixes, sides, existing names), AIData, the string
  file and the skirmish scripts; then mount the assets (refused when a path exists already),
  add the strings, and load the definition files with the engine's own INI reader.
  The ruleset is `starter` when the game data defines the faction `FactionIronwood` and has no
  `FactionAmerica` (the starter pack), `zerohour` otherwise.
- A package that fails one of the checks is skipped as a whole (nothing of it is added) and the
  game carries on. A failure that only shows while the INI reader parses a definition (a bad
  value, an unknown science) cannot be undone: the engine writes the report (status `failed`) and
  stops through its fatal-error path (`Fatal error: The army package '<id>' could not be loaded
  completely ...` on stderr), instead of starting with half an army.
- Results go to the log (and to the browser console) as one line per package,
  `ZHARMY: <id> loaded|skipped|failed: <reason>`, and to `/userdata/ArmyReport.json`
  (`<user data folder>/ArmyReport.json`), which the launcher shows. It is written on every start,
  also without packages:

```json
{
  "ruleset": "starter",
  "packages": [
    { "id": "test.ironwood-irb", "path": "/armies/irb.zharmy", "status": "loaded", "reason": "",
      "tag": "IRB", "name": "...", "version": "1.0.0",
      "factions": [ { "playerTemplate": "IRB_FactionIronwood", "side": "IRB_Ironwood",
                      "displayName": "Ironwood Compact (IRB)", "ai": true } ] }
  ]
}
```

  `status` is `loaded`, `skipped` (with `reason`) or `failed` (the game stops). The file is written
  while the engine starts (before the first frame), so a launcher that waits for it should delete an
  old one before each start and poll. Packages are
  listed in load order; a package whose manifest could not be read has its file name as `id`.
- The factions appear in the skirmish faction list (name from `SIDE:<side>`) next to the standard
  ones and can be chosen for human slots. A faction with `ai: false` is not offered to computer
  slots (a slot that has it when it becomes a computer slot, or at the start of a match, is set
  to "Random"); "Random" for a computer slot never picks it. For a computer-playable faction the
  engine adds a skirmish side `Skirmish<side>` to every match, with the package's scripts.

- Tests: `scripts/zharmy_testpkg/army_flow.mjs` drives the real game in headless Chromium with
  packages (`--army`, `--userfile`, see its header); `scripts/zharmy_testpkg/make_negative.py` writes the
  broken packages (redefined names, duplicate tag, shadowed file, requires mismatch, corrupt zip, bad
  manifest, hash mismatch, parse failure, and the `Crate.ini` / `ZHC<TAG>` cases) from converter output.

## Converter (`tools/zharmy`)

Python 3, standard library only, runs on the player's machine (and later in the
browser). `tools/zharmy/README.md` has the commands and the details.

- `zharmy archives <game folder>`: lists the `.big` files in the order the engine loads them
  (Zero Hour's archives, then the base game's) and shows which would be taken as the mod's.
- `zharmy find <folder...> <glob>`: lists which archive or loose file provides matching paths.
- `zharmy inspect <mod folder | .big files>... [--base ...] [--mod-archives ...]`: lists the playable
  factions of a mod, the string table language chosen, and a summary of INI problems
  (`--ini-problems` lists them all).
- `zharmy convert <mod> --base <ruleset data> --faction <PlayerTemplate> --tag <TAG> -o <file>`
  and `zharmy convert-all`: follow every reference from the faction's `PlayerTemplate` (command
  sets, buttons, units and buildings, weapons, locomotors, armour, upgrades, sciences, special
  powers, OCLs, FX, particle systems, models, textures, sounds, strings, AI build list and
  scripts), keep what the ruleset already has as references, copy and rename the rest into the
  package, and write a report of anything it could not carry over. A mod installed in the game
  folder is selected with `--mod-archives auto` (every archive that has no retail file name;
  the default in that case) or with name patterns; a pattern that matches nothing is an error.
  Files are looked up where the engine looks (language folders, `.dds` before `.tga`, `.wav`
  sounds, `FILE.PART` model names). References the mod itself cannot resolve are warnings counted
  apart as pre-existing dangling references, except sciences, command buttons and locomotors,
  which make the faction "cannot be converted"; a faction whose starting building or unit does
  not exist is skipped.
- `zharmy validate <file> [--base <ruleset data>] [--mod <mod> | --mod-archives auto]`: checks
  a package against the rules above, the same way the engine does. Without `--mod` /
  `--mod-archives` every missing reference is an error (the mod as played is not known).

## Not in v1

Multiplayer with packages (needs the lobby to compare `id` + `contentHash`), new
behaviour code (a later scripting layer), rulesets other than `zerohour` and `starter`,
and mods that need a modified game executable.

## Open questions (from the converter)

Found while writing `tools/zharmy`. Resolved in the format above: 1 (contentHash), 2 (rule 7),
3 (rule 4 accepts `ZHC<TAG>`; the converter writes it by default), 4 (`Crate.ini` added to the
table), 8 (rule 2). The others describe converter behaviour.

1. **contentHash and letter case.** The text sorts by lower-cased name but says "`name`" when feeding the hash.
   The converter writes every entry name in lower case, so both readings give the same hash for its packages;
   `validate` hashes the lower-cased name. Hand-made packages with mixed-case names would differ.
2. **Skirmish scripts: player name.** `Player::initFromDict` takes a script list from a *map* skirmish side whose
   faction's side equals the AI player's side, so a side that no map knows (`<TAG>_<Side>`) has no list to pick.
   The converter writes the list for player `Skirmish<TAG>_<Side>` (the starter pack's convention is
   `Skirmish<Side>`) with its teams owned by that player; scripts, teams, counters and flags are prefixed `<TAG>_`.
   The loader has to hand that list to every AI player of the side.
3. **House colour.** The asset manager decides team colour from the texture file name (`ZHC...`, W3DAssetManager.cpp)
   and from mesh names (`HOUSECOLOR...`, `TREADS...`). Rule 4 (file names start with the tag) therefore loses the
   texture based team colour. Mesh names are kept (only the container part of a full name is renamed). Option
   `--zhc-names` writes `ZHC<TAG>...` instead; it breaks rule 4 as written, so it needs a contract change
   (accept `ZHC<TAG>` as a prefix) before it can be the default.
4. **Extra file names.** Besides the list in "Definitions" the converter writes `Music.ini` (MusicTrack),
   `Speech.ini` (DialogEvent), `Crate.ini` (CrateData). The loader should take any `Army/INI/*.ini` and order by
   type.
5. **Localized sound files** (`Data/Audio/Sounds/<Language>/...`) are copied to the generic folder (the language of
   the chosen string table), because the engine tries the localized path first and then the generic one.
6. **Reskins.** `ObjectReskin` cannot set a side; a reskin of an object on another side keeps that side (reported).
7. **Strings.** The engine reads `.str` as single byte text; other characters become `?` (reported).
8. **Ruleset references** may point to ruleset objects of *other* sides (for example a stock tank an HQ can build);
   rule 3 only restricts what the package adds.
