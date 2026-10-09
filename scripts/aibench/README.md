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

## Requirements

* A web build of the game: `source emsdk/emsdk_env.sh; cmake --preset emscripten -B build/bench -DRTS_WEB_FFMPEG=OFF;
  ninja -C build/bench -j2 z_generals starter_pack`. The directory to pass is `build/bench/GeneralsMD` (it has
  `z_generals.html`). `starter_pack` puts the free starter content next to it.
* Node.js 20 or newer and the `playwright` npm package with a Chromium 137 or newer (the engine needs JSPI).
  On the Linux sandbox: `NODE_PATH=/opt/node22/lib/node_modules`, Chromium from `/opt/pw-browsers`. Elsewhere
  `npm i playwright && npx playwright install chromium`, or `--chromium /path/to/chrome`.

## Options

`node aibench.mjs --help` lists them all. The important ones:

| option | meaning |
|---|---|
| `--site DIR` | the build under test (reported as `candidate`) |
| `--baseline DIR` | a second build to compare against (reported as `baseline`); both are played with identical matches |
| `--build NAME=DIR` | any number of named builds; the first one is the baseline of the comparison |
| `--data starter\|DIR` | the free starter content (default), or a folder holding `ZeroHour/` (+ optional `Generals/`) |
| `--map NAME` | repeatable. Folder/file name, display name or path of the map |
| `--matchup "d:side,d:side"` | repeatable. Players of one match, see below |
| `--seeds N`, `--seed-start N` | seeds `start..start+N-1` per matchup and map |
| `--starts 1,2` / `--no-rotate` | start positions; by default every seed shifts the players by one position, so each configuration plays from each start position equally often |
| `--timeout MIN` | game minutes before a match is declared a timeout (default 20) |
| `--workers N` | matches at the same time. Each is a full engine instance: 1 per core is a good start; real game data needs about 1 GB per worker |
| `--determinism N\|all` | matches replayed to check determinism (default 2) |
| `--target "A>B=P"` | expected win rate of A against B; the summary says whether the measured rate (with its interval) meets it |
| `--engine-arg ARG` | an extra engine argument for every match, e.g. `cash=20000`, `sample=60`, `loop=engine`, `eliminate=1@4000` |
| `--probe` | prints the maps and sides found in the game data |
| `--profile DIR`, `--port N` | browser profile and port (keep them fixed between runs) |

### Players: `difficulty:side[:variant]`

* difficulty: `easy`, `normal`, `hard`, `expert`. Different difficulties can play in one match
  (`expert:Ironwood,hard:Ironwood`). `hard` is the original Hard AI.
* side: the name of a player template (`FactionAmerica`, or `America`), a side shared by several templates (the first
  one is taken), or `random` (decided by the seed). Use `--probe` to list them.
* variant: a free tag the AI code can read to run an experimental variant next to the standard AI in one match:
  `AIMatch::getPlayerVariant(player)` in `Common/AIMatch.h` (empty outside the bench).
* More than two players: list them all, the map must have room; every player is on his own team unless the engine
  command line (`players=` field 3, below) says otherwise. The runner's report tables are for 1v1 matchups.

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
2. Make a folder with your installs, e.g. `~/zh-data/ZeroHour` (the Zero Hour install: it contains `INIZH.big`,
   `W3DZH.big`, `TexturesZH.big`, ...) and `~/zh-data/Generals` (optional: the original Generals install with `INI.big`,
   `W3D.big`, ... Zero Hour loads its archives too; videos and installers are skipped).
3. Install Playwright (`npm i -g playwright && npx playwright install chromium`) and run, for example:

```
node scripts/aibench/aibench.mjs --site build/web/GeneralsMD --data ~/zh-data \
     --profile ~/.cache/zh-aibench-profile --probe
node scripts/aibench/aibench.mjs --baseline build/web-old/GeneralsMD --site build/web/GeneralsMD --data ~/zh-data \
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

## Files

```
scripts/aibench/aibench.mjs       the command line runner
scripts/aibench/lib/server.mjs    static server for the builds (one origin, isolation headers)
scripts/aibench/lib/browser.mjs   Playwright: data import, one match per page, result capture
scripts/aibench/lib/stats.mjs     win rates, Wilson intervals, Elo, determinism comparison
scripts/aibench/lib/report.mjs    report.json / report.md
scripts/aibench/test/stats.test.mjs   tests of the statistics:  node --test scripts/aibench/test
GeneralsMD/Code/GameEngine/Source/Common/AIMatch.cpp, Include/Common/AIMatch.h   the engine mode
```

Engine hooks outside the module: `-aiMatch` in `CommandLine.cpp` (implies headless), `GameMain.cpp` and `WebMain.cpp`
(start the mode), `GameLogic.cpp` (no load screen; CRC messages into the command list), `Recorder.cpp` (no replay
unless `record=1`), `Money.h/.cpp` and `ScoreKeeper.h` (read access to the tallies).
