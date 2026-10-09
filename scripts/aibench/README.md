# AI test bench (aibench)

Plays computer-vs-computer matches of the WebAssembly build of the game, headless and as fast as the CPU allows,
in parallel, and reports win rates (with confidence intervals), Elo, speed and a determinism check. It is how
changes to the skirmish AI are measured: run the same matchups with the old and the new build and compare.

```
node scripts/aibench/aibench.mjs --site build/bench/GeneralsMD --data starter \
     --matchup "expert:Ironwood,hard:Ironwood" --seeds 20 --workers 2 \
     --target "expert:Ironwood>hard:Ironwood=0.8"
```

## How it works

* **Engine side**: `-aiMatch` (`GeneralsMD/Code/GameEngine/Source/Common/AIMatch.cpp`, header `Include/Common/AIMatch.h`).
  It implies `-headless`, builds a skirmish from the command line (only computer players, no menus, no load screen),
  seeds the game logic random numbers with the seed, then calls `TheGameLogic->UPDATE()` in a loop: no rendering, no
  message stream, no frame pacing. It stops when the victory conditions decide the match or after the time limit,
  prints one line `AIMATCH_RESULT {json}` (and writes the file given by `stats=`), and exits with code 0 (1 if the
  match could not be played; the line is then `AIMATCH_ERROR <reason>`).
  It uses only the command line and the game data as input, so a match is exactly reproducible with the same build.
* **Runner side**: `aibench.mjs` serves the web build(s) from a local server, starts headless Chromium (Playwright)
  and plays each match in its own page (its own engine instance, one match per process). It collects the result
  line, aggregates, replays a few matches to check determinism, and writes `report.md` and `report.json`.
* The game data stays in the browser profile (OPFS): the starter content is downloaded from the build once, a real
  install is copied once. Use the same `--profile` (and the same `--port`, which is part of the browser origin) and
  later runs start at once.
* **Native runner** (`--native`): the same matches without a browser, with the native headless build (`zh_headless`,
  see below). Each match is its own process that reads the game data in place; there is nothing to import.

## Requirements

* A web build of the game: `source emsdk/emsdk_env.sh; cmake --preset emscripten -B build/bench -DRTS_WEB_FFMPEG=OFF;
  ninja -C build/bench -j2 z_generals starter_pack`. The directory to pass is `build/bench/GeneralsMD` (it has
  `z_generals.html`). `starter_pack` puts the free starter content next to it.
* Node.js 20 or newer and the `playwright` npm package with a Chromium 137 or newer (the engine needs JSPI).
  On the Linux sandbox: `NODE_PATH=/opt/node22/lib/node_modules`, Chromium from `/opt/pw-browsers`. Elsewhere
  `npm i playwright && npx playwright install chromium`, or `--chromium /path/to/chrome`.

## The native headless build (`--native`)

`zh_headless` is the game logic of the web build compiled for Linux or macOS with the system's clang: the same engine
and the same Win32 stand-ins (`Dependencies/WebCompat`), without window, renderer, audio or video
(`cmake/native-headless.cmake`, `GeneralsMD/Code/Main/NativeMain.cpp`). It is a 64-bit program. It runs `-aiMatch`
(and `-simulateReplay`) and prints the same `AIMATCH_RESULT` line.

Install: Linux: `clang`, `cmake` (3.28 or newer), `ninja`, `python3` (`apt install clang cmake ninja-build python3`).
macOS: the Xcode command line tools (`xcode-select --install`), and `brew install cmake ninja`.

```
cmake --preset native-headless                      # build/native-headless
cmake --build build/native-headless --target zh_headless starter_pack
build/native-headless/GeneralsMD/zh_headless --zh build/native-headless/GeneralsMD/starterpack \
    -aiMatch "map=Ironwood Crossing" players=hard:Ironwood,hard:Ironwood seed=1 timeout=5
```

`--zh DIR` is the Zero Hour folder (default `$ZH_PATH`), `--generals DIR` the original Generals (default
`$GENERALS_PATH`, optional), `--userdata DIR` where the options and replays go (default: a temporary folder that is
removed at the end, so parallel matches do not share it). The files are read in place with the Windows file names
matched case-insensitively. The bench:

```
node scripts/aibench/aibench.mjs --native build/native-headless/GeneralsMD/zh_headless --data starter --seeds 8 --workers 2
node scripts/aibench/aibench.mjs --native build/native-headless --zh "$ZH_PATH" --map tournamenta --matchup "expert:China,hard:China"
```

`--data starter` uses `starterpack/` next to the executable (the `starter_pack` target). `--native` can be combined with
`--site` to play the same matches in both. Their results are not bit-identical: the game calls the C library's `sinf`,
`cosf`, `acosf` ... and those of glibc or macOS differ in the last bit from Emscripten's (musl), which changes the
CRC from the first frame on (the game state has the same layout in both builds; only such float values differ).
Each build is deterministic on its own, and Linux and macOS may differ from each other for the same reason.
`--boot` needs a web build.
`scripts/gameplay/realdata_tests.sh` uses the native build when `build/native-headless` exists.

## Options

`node aibench.mjs --help` lists them all. The important ones:

| option | meaning |
|---|---|
| `--site DIR` | the build under test (reported as `candidate`) |
| `--baseline DIR` | a second build to compare against (reported as `baseline`); both are played with identical matches |
| `--build NAME=DIR` | any number of named builds; the first one is the baseline of the comparison |
| `--native PATH` | the native headless build (`zh_headless` or its directory), reported as `native`; no browser needed |
| `--data starter\|DIR` | the free starter content (default), a Zero Hour install, or a folder holding `ZeroHour/` (+ optional `Generals/`) |
| `--zh DIR` / `--generals DIR` | the Zero Hour install and (optional) the original Generals install; default `$ZH_PATH` / `$GENERALS_PATH` |
| `--map NAME` | repeatable. Folder/file name, display name or path of the map |
| `--matchup "d:side,d:side"` | repeatable. Players of one match, see below |
| `--seeds N`, `--seed-start N` | seeds `start..start+N-1` per matchup and map |
| `--starts 1,2` / `--no-rotate` | start positions; by default every seed shifts the players by one position, so each configuration plays from each start position equally often |
| `--timeout MIN` | game minutes before a match is declared a timeout (default 20) |
| `--workers N` | matches at the same time. Each is a full engine instance: 1 per core is a good start; real game data needs about 1 GB per worker |
| `--determinism N\|all` | matches replayed to check determinism (default 2) |
| `--target "A>B=P"` | expected win rate of A against B; the summary says whether the measured rate (with its interval) meets it |
| `--keep-logs` | writes the engine output of every match to `matches/<id>.log` (default: only failed matches). Needed to read the `trace` output of a player |
| `--overlay NAME[,NAME]` | plays on the starter content edited by `scripts/aibench/fixtures/NAME.json` (see "Overlays"); uses its own browser profile (`<profile>-overlay-NAME`, so give a fresh `--profile` after editing a fixture) |
| `--engine-arg ARG` | an extra engine argument for every match, e.g. `cash=20000`, `sample=60`, `loop=engine`, `eliminate=1@4000` |
| `--probe` | prints the maps and sides found in the game data |
| `--profile DIR`, `--port N` | browser profile and port (keep them fixed between runs) |

### Players: `difficulty:side[:variant][@team]`

* difficulty: `easy`, `normal`, `hard`, `expert`. Different difficulties can play in one match
  (`expert:Ironwood,hard:Ironwood`). `hard` is the original Hard AI.
* side: the name of a player template (`FactionAmerica`, or `America`), a side shared by several templates (the first
  one is taken), or `random` (decided by the seed). Use `--probe` to list them.
* variant: a free tag the AI code can read to run an experimental variant next to the standard AI in one match:
  `AIMatch::getPlayerVariant(player)` in `Common/AIMatch.h` (empty outside the bench). The Expert AI reads a list of words
  joined by `+` (`AIStrategy::newMap`): `trace` prints its decisions (`AISTRAT[...]` lines; add `--keep-logs`),
  `off-split+kite` switches single Expert features off for an A/B run (`expert:Ironwood:off-merge` against `expert:Ironwood`),
  `on-kite` switches on a feature whose default is off. The words are `focus wave retreat scout counter save starve siege defend`
  and the tactics `split threat kite fight merge spread`. Batch 2: `raid protect repair route basedef geo layout garrison clear ability
  airborne bunker team bdcap` (`geo` has two parts that can be switched off on their own: `georally` and `geosites`).
* team: `@N` after the player (`expert:China:trace@1`) puts it on team N; players of a team are allies. Without it every
  player is on his own team.
* More than two players: list them all, the map must have room. Team games: use `--no-rotate --starts a,b,c,d` so each
  team keeps its side of the map. The runner's report tables are for 1v1 matchups; the per-match files have every player.

**About `expert`**: it is the engine's `SLOT_EXPERT_AI` slot state (the level above Hard), mapped in `parsePlayers()` in
`AIMatch.cpp`. A new level needs one line there and the name in `aibench.mjs` is free-form, so nothing else changes.

### Engine command line

The runner builds this for you; it is also what you run by hand (in the browser: `?arg=-aiMatch&arg=map%3D...`,
on a native build: after the executable):

```
-aiMatch map=<map> players=<difficulty>:<side>[:<team>[:<start>[:<variant>]]],... seed=<n>
         [timeout=<game minutes> | maxframes=<logic frames>] [stats=<file.json>] [crcinterval=<frames>]
         [sample=<frames>] [idleinterval=<frames>] [progress=<frames>] [cash=<money>] [label=<text>] [record=1]
```

`start` is 1-based (0 or empty: random), `team` is a number (empty: no team). `seed` is required. Unknown options are
an error. Matches are not recorded as replays unless `record=1` (parallel matches would write the same file).

## What is measured

Per match (`matches/<id>.json`, schema `zh-aibench-1`), per player:

* outcome (`won`/`lost`/`timeout`/`draw`) and rank; the frame the player was defeated
* money: final, gathered (harvesting, bounties, deposits), withdrawn, refunded (cancelled production, sales), spent
* units and structures built, lost and objects destroyed, by template name (the game's own `ScoreKeeper`)
* army value over time (cost of living units that can attack), other units, structures, supply centres and supply
  sources held (sources nearest to the player's supply centre), at `sample` intervals (default 10 s of game time)
* idle production: factory-frames and the share in which the factory's queue was empty (sampled every 0.5 s)
* the game CRC every `crcinterval` frames (default 300) and at the end, `result.finalCRC`
* `perf`: setup time, simulation time and logic frames per second (not deterministic, not compared)

Report: win rates with Wilson 95 % intervals, win-rate matrix per build, Elo (maximum likelihood, timeouts and
draws as half a win), start-position bias, per-configuration averages, speed, and with two builds a side-by-side
comparison. **Only real victories count as wins.** A timeout is reported separately (and is half a game in the Elo);
errors are listed and not counted.

### Determinism

Selected matches are played a second time (new engine instance, same build and command line). The CRC timelines must
be identical; otherwise the report names the frame window in which the two runs first differ
(`lastGoodFrame` < divergence <= `atFrame`) and the exit code is 1. The statistics must also be identical.
Use `--crc-interval 30` to narrow the window when hunting a desync, then rerun with a smaller `--seeds`.

### Engine options for tests

* `loop=engine` runs the whole engine update (client, message stream) instead of the game logic alone. Results must
  be identical; if they ever differ, the bench no longer plays the game the way players do.
* `eliminate=<slot>@<frame>` (repeatable) kills everything of a player at a frame. It exists to exercise the end of
  a match (defeat, victory, ranks, the reports) where the AI does not fight.

## Overlays: starter data that exercises more of the AI

The starter faction has two infantry units, a scout and a tank, no static defences and weapons with a blast of 8-16 units, so a few
behaviours cannot trigger on it. An overlay is a small JSON file in `scripts/aibench/fixtures/` that edits the *copy* of the starter pack
the bench serves (find/replace or append on the pack's INI files; the repository's data is untouched; the edits are our own original data):

```
{ "description": "...", "edits": [ { "file": "data/ini/weapon.ini", "find": "text that occurs once", "replace": "new text" },
                                    { "file": "data/ini/weapon.ini", "append": "Weapon NewOne ... End" } ] }
```

| overlay | what it changes | what it is for |
|---|---|---|
| `towers` | the power plant and the barracks become gun towers (range 200, turret) | static defences: the fight check before and during a wave |
| `strictfight` | `ExpertSkill` thresholds of the fight check raised | forces the hold and the forced launch of a wave |
| `splash` | the rocketeer's weapon becomes artillery (range 220, blast 35/60) | enemy area weapons: spreading out |
| `openmap` | the headquarters sees the whole map (vision and shroud clearing range 5000) | everything that needs *seen* targets: raids, routes, breaches, airborne drops |
| `haulers` | the headquarters sends out a parked supply hauler (KindOf HARVESTER, unarmed) 170 units from the base every 20 s; it lives 90 s | gatherers outside the base: economic raids, worker protection |
| `raiders` | the rocketeer runs at speed 75 (the army averages about 40) | fast units for the raiding party |
| `repairpads` | the factory is a repair pad (`RepairDockUpdate`, KindOf REPAIR_PAD), the power plant a clinic (`HealContain`, HEAL_PAD, 4 infantry); the infantry get a transport slot | repair and heal trips |
| `outposts` | the headquarters puts an armed outpost (range 220, lives 150 s, renewed every 140 s) 330 units out in the field | a defence on the way of the waves: routes around defences |
| `longrockets` | the rocketeer's weapon reaches 260 | units that out-range a defence (combine with `towers`): breaches |
| `garrison` | the power plant can be garrisoned (`GarrisonContain`, 4 infantry who shoot from inside); the infantry get a transport slot | garrisons when attacked, clearing enemy garrisons |
| `bunker` | the headquarters puts two bunkers near the base (a structure with a `TransportContain` for 4 infantry, `PassengersAllowedToFire = Yes`, not armed itself, lives 80 s, renewed every 90 s); the infantry get a transport slot | standing garrisons: filling and refilling the bunkers (`bunker`) |
| `ability` | the rocketeer has a targeted special power (the defector power: an enemy unit changes sides; 30 s reload; `NEED_TARGET_ENEMY_OBJECT`) | unit abilities |
| `airlift` | the scout is an unarmed transport helicopter (KindOf AIRCRAFT TRANSPORT, `TransportContain` for 4 infantry, hovers 10 units up); the infantry get a transport slot | airborne insertion |
| `antiair` | with `haulers`: the hauler carries a surface-to-air launcher (range 220, hits aircraft only) | seen anti-air coverage for the flight path |
| `geotowers` | three defence towers (range 200; KindOf FS_BASE_DEFENSE and FS_POWER, so that the skirmish build code builds them by itself) on the Ironwood build list at fixed places, and a build button in the worker's command set | where defence structures are put: terrain-aware defence (`geo`) |
| `teams` | adds the map **Ironwood Teams** (`--map ironwood_teams`, `fixtures/maps/teams.py`): six start positions in two team areas on a 2240 x 2240 field, no cliffs | team games: `--starts 1,2,5,6 --no-rotate`, players with `@team` |
| `sprawl` | eight more entries on the Ironwood build list (power plants, barracks, factories) 250-420 units from the headquarters: the base radius becomes about 430 | big bases as in a long game with a lot of cash (use with `--engine-arg cash=50000`) |
| `gates` | adds the map **Ironwood Gates** (`--map ironwood_gates`, generated by `fixtures/maps/gates.py`): each base lies in a basin with a cliff rim and four gaps (a wide one facing the enemy, two narrow ones at the sides, a dead end behind) | ways into the base and chokepoints: terrain-aware defence |

An edit can also be `{ "generate": "maps/gates.py" }`: the script is run with an output folder, and the files it writes there are added to the
copy of the pack at the same paths (a new map and the like); text files under `append/` are appended to the pack file of the same path
(`append/maps/mapcache.ini`). The browser profile keeps the starter data between runs and does not notice a changed fixture: give a fresh
`--profile` after editing one.

Overlays can be combined (`--overlay towers,strictfight`). Example: `--overlay splash --matchup "expert:Ironwood,expert:Ironwood:off-spread"`.
A fixture edit must find its text exactly once, otherwise the bench stops with an error.

## Tests of the bench itself

* `node --test scripts/aibench/test/stats.test.mjs`: statistics (Wilson interval, Elo, matrix, determinism compare).
* `node scripts/aibench/test/mock.test.mjs`: the whole runner against a mock engine page (no game needed).

## Limits of the starter content

The free starter content has **one faction (Ironwood Compact) and one two-player map (Ironwood Crossing)** with
a simple economy (the HQ pays an income, no supply gathering): all matches are 1v1 mirror matches of one faction,
so supply-source and faction-specific metrics read zero and the matrix has one cell per difficulty pair. That
is enough to test mechanics and determinism; tune against the real factions with the real data (below). A map with
more start positions would be needed to test 2v2 and team play.

**Known problem (content/AI, not the bench):** with the starter content the computer players build the HQ's power plant
and two workers and then never build anything else, on any difficulty, for as long as 25 game minutes (money piles up,
nothing is produced, so every match ends in a timeout and `idle production` reads 100 %). `loop=engine` behaves the same,
so it is not an artefact of the headless loop. Until the starter AI (`AIData.ini` `SkirmishBuildList`, `SkirmishScripts.scb`)
or the skirmish AI builds a base there, the starter content only exercises start-up, determinism and the reporting;
use `eliminate=` to test the end of a match, and the real game data for AI measurements.

## Running with the real game data (Mac or any machine)

1. Get the web build: build it (`emsdk`, `cmake --preset emscripten ...` as above, which also works on macOS), or take
   the build artifact of the project's web build. You need the directory with `z_generals.html`, `z_generals.js`,
   `z_generals.wasm` (and workers).
2. Point the bench at your installs: `export ZH_PATH="/path/to/Zero Hour"` (the folder with `INIZH.big`, `W3DZH.big`,
   ...) and, optionally, `export GENERALS_PATH="/path/to/Generals"` (the original Generals install with `INI.big`,
   `W3D.big`, ... Zero Hour loads its archives too; videos and installers are skipped). `--zh` / `--generals` do the
   same on the command line; `--data DIR` with `DIR/ZeroHour` (+ `DIR/Generals`) in it still works. With `ZH_PATH`
   set, the bench uses your game data unless you pass `--data starter`.
3. Install Playwright (`npm i -g playwright && npx playwright install chromium`) and run, for example:

```
node scripts/aibench/aibench.mjs --site build/web/GeneralsMD --profile ~/.cache/zh-aibench-profile --probe
node scripts/aibench/aibench.mjs --baseline build/web-old/GeneralsMD --site build/web/GeneralsMD \
     --map "Tournament Desert" --map "Tournament Island" \
     --matchup "expert:America,hard:America" --matchup "expert:China,hard:GLA" \
     --seeds 10 --workers 3 --timeout 25 --target "expert:America>hard:America=0.8"
```

   The first run copies the files into the browser profile (several GB, minutes); later runs reuse them. `--probe` prints
   the exact map and side names. Map names are matched by folder/file name or display name, so the standard 1v1/2v2
   maps work by their usual names; sides are the factions (`America`, `China`, `GLA`) or the generals
   (`AmericaAirForceGeneral`, ...). Games on the real maps last longer than on the starter map: raise `--timeout`.
4. Open `aibench-out/<time>/report.md`.

Notes: use the same `--port` and `--profile` every time (browser storage belongs to the origin and profile); close
other copies of the page; each worker needs memory for a full game (roughly 1 GB) and a core.

## Player assists: the sibling bench

`-assistMatch` is the same kind of mode for the player assists: a skirmish with one human player, driven by a script of
timed steps (create units, select them, send assist and ordinary orders as the user interface does, let an enemy attack)
with checks on the game state, at full logic speed. Its runner, `scripts/assistbench/assistbench.mjs`, takes the same game
data options and reuses `lib/server.mjs` and `lib/browser.mjs` (`runMatch(..., 'ASSISTMATCH')`). The real-data scenarios
run in `scripts/gameplay/realdata_tests.sh assists`. See `scripts/assistbench/README.md`.

## Files

```
scripts/aibench/aibench.mjs       the command line runner
scripts/aibench/lib/server.mjs    static server for the builds (one origin, isolation headers)
scripts/aibench/lib/browser.mjs   Playwright: data import, one match per page, result capture
scripts/aibench/lib/native.mjs    --native: one zh_headless process per match, result capture
scripts/aibench/lib/stats.mjs     win rates, Wilson intervals, Elo, determinism comparison
scripts/aibench/lib/overlay.mjs   --overlay: an edited copy of the starter pack
scripts/aibench/fixtures/*.json   the overlays
scripts/aibench/fixtures/maps/     generators of overlay maps (gates.py)
scripts/aibench/lib/report.mjs    report.json / report.md
scripts/aibench/test/stats.test.mjs   tests of the statistics:  node --test scripts/aibench/test
GeneralsMD/Code/GameEngine/Source/Common/AIMatch.cpp, Include/Common/AIMatch.h   the engine mode
GeneralsMD/Code/GameEngine/Include/Common/AIMatchShared.h   its parts that -assistMatch reuses (map/side lookup, JSON, bench flag)
```

Engine hooks outside the module: `-aiMatch` in `CommandLine.cpp` (implies headless), `GameMain.cpp`, `WebMain.cpp` and
`NativeMain.cpp` (start the mode), `GameLogic.cpp` (no load screen; CRC messages into the command list), `Recorder.cpp` (no replay
unless `record=1`), `Money.h/.cpp` and `ScoreKeeper.h` (read access to the tallies).
