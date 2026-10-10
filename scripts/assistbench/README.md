# Player assist test bench (assistbench)

Scripted checks of the player assists (formations, protect links, and the assists whose orders are sent with `send`) at
full logic speed, without rendering and without clicks. One scenario takes a few seconds instead of the 15 minutes or
more of a click-through flow in a software-rendered browser (`GeneralsMD/Code/Main/web/test/assist_flows.sh`).

Gameplay is judged only on the real Zero Hour data (`CLAUDE.md`). The scenarios in `scenarios/realdata` are the ones that
count. The ones in `scenarios/starter` are smoke checks: they show that the bench runs, not that an assist plays well.

## Run it

```sh
# on the real data (your own Zero Hour install; nothing is copied out of the browser profile)
ZH_PATH="/path/to/Zero Hour" node scripts/assistbench/assistbench.mjs --site build/web/GeneralsMD
# or as part of the gameplay tests (report in test-reports/<date>-<commit>/summary.md and assists/)
ZH_PATH="/path/to/Zero Hour" scripts/gameplay/realdata_tests.sh assists

# one scenario, or a few by name
node scripts/assistbench/assistbench.mjs --site build/web/GeneralsMD --zh "$ZH_PATH" scripts/assistbench/scenarios/realdata/protect-unit.json
node scripts/assistbench/assistbench.mjs --site build/web/GeneralsMD --zh "$ZH_PATH" --filter 'formation-(line|drag)'

# smoke check of the bench on the starter content (a development aid)
node scripts/assistbench/assistbench.mjs --site build/web/GeneralsMD

# the same with the native headless build (no browser; cmake --preset native-headless, targets zh_headless starter_pack)
node scripts/assistbench/assistbench.mjs --native build/native-headless
```

The build is the usual web build (`scripts/web/run.sh --build-only`, or `ninja -C build/web z_generals starter_pack`), or
with `--native` the native headless build (`zh_headless`, its game data read in place; see `scripts/aibench/README.md`).
The two builds can differ in the last bit of some floating point results, so a scenario that passes in one may land a
unit a little differently in the other.
The game data options are those of `aibench.mjs`: `--zh`/`$ZH_PATH`, `--generals`/`$GENERALS_PATH`, `--data starter|DIR`,
`--profile` (the browser profile that keeps the imported data; the default is the one aibench uses). `--workers N` runs N
scenarios at a time (default 2), `--keep-logs` keeps every engine log, `--out DIR` sets the report folder.

The report folder has `report.md` (pass/fail per scenario, the first failing check), `report.json` (every check with its
measured values), `<scenario>.trace.txt` (the steps, the checks and the `ASSIST` decision lines) and, for a scenario that
did not pass, `<scenario>.log` (the whole engine output).

## Scenarios

A scenario is a JSON file:

```json
{
 "name": "protect-unit",
 "description": "what it checks, for the report",
 "map": "tournamenta",
 "players": "human:America:-1:1,idle:China:-1:2",
 "seed": 1,
 "steps": ["spawn guards 0:AmericaInfantryRanger:4 at=cc:0^220,-60", "wait 5", "select guards", "protect guards vip", "..."],
 "log": ["^ASSIST protect \\d+ answers the alarm"],
 "assists": true,
 "expect": "pass"
}
```

- `players`: `kind:side[:team[:start]]`, the human player first (slot 0, the local player whose assists are allowed), then
  `idle` (a computer player without its AI: it never moves on its own) or `easy|normal|hard|expert` computer players.
- `log`: lines the engine must print (regular expressions), or `{ "match": "...", "min": 1, "max": 3 }`; `max: 0` forbids one.
- `assists: false` plays a match that does not allow the assists. `expect: "fail"` marks a scenario that must fail (checks
  of the bench itself). `maxFrames`, `cash` and `engineArgs` are passed on.

### Steps

Steps run in order, in the same logic frame, until one waits. The orders of the human player are the game messages the user
interface sends, appended to the command list for the local player, so they go through the selection, the message
dispatcher and the assists exactly as clicks and hotkeys do (and as a replay would replay them).

| step | what it does |
|---|---|
| `spawn <name> <entry>[+<entry>] [at=<pos>]` | creates units with `-assistTest` entries (`player:template:count[:ref:dx:dy]`); `at=` puts the first one there. The template may be `@KINDOF` (`@REPAIR_PAD`, `@HEAL_PAD`): the first structure template with that KindOf, of the player's side if the data has one, else of any side; the trace line `ASSISTTEST created N x <template>` names it |
| `name <name> <objects>` | names a set of objects |
| `select <objects>` | MSG_CREATE_SELECTED_GROUP |
| `group <n>` / `selectgroup <n>` | MSG_CREATE_TEAMn from the selection / MSG_SELECT_TEAMn |
| `formation <type>` | MSG_ASSIST_FORMATION: none, line, column, wedge, box, loose, keep |
| `fmove <posA> [<posB>] [type=] [attack=1]` | MSG_ASSIST_FORMATION_MOVE, a right-drag from A to B (the type is the selection's formation, as in the UI) |
| `move` / `attackmove` / `guard <pos>`, `attack <objects>`, `stop` | the ordinary orders (a right click) |
| `protect <protectors> <protected>` or `protect <protectors> group=<n>` | MSG_ASSIST_PROTECT |
| `unprotect` | MSG_ASSIST_UNPROTECT for the selection |
| `idle army\|all\|workers` | what the idle hotkeys do: the same pick (`PlayerAssist::pickIdle`), then MSG_CREATE_SELECTED_GROUP |
| `produce <building> <template>` | selects the building and queues the unit, as the command bar does (MSG_QUEUE_UNIT_CREATE) |
| `repeat <buildings> on\|off [reserve=<money>]`, `repeat - reserve reserve=<money>` | MSG_ASSIST_REPEAT_PRODUCTION (the reserve is kept for the next ones; default 0) |
| `money <slot> <amount>` | sets a player's money (a test setup) |
| `send <command> [int:\|bool:\|real:\|pos:\|obj:]...` | any command of the player by name, e.g. `send ASSIST_STANCE int:2 int:2 int:70` |
| `ai <objects> attack <objects>` / `move\|attackmove\|guard <pos>` / `stop` | direct orders for any player's units (attackers) |
| `damage <objects> <amount>[%] [by=<objects>]` | damage as if `by` had hit them (raises a protect alarm) |
| `kill <objects>`, `snapshot <objects> [as=]`, `dump <objects>` | remove, remember where units stand, print their state |
| `wait <frames>` | 30 frames are a second |
| `until [not] <condition> [max=<frames>]` | waits for the condition; a check that fails after `max` (default 900) |
| `expect [not] <condition>` | a check now |

Objects: a name, `name[i]`, `cc:<slot>` (that player's command center), `sel` (the selection), `all:<slot>:<template>`
(every live object of that template the player owns, by id; may be none), joined with `+`.
Positions: `x,y`, a set of objects (its centre) or `map` (the map's centre), with an optional offset: `+dx,dy` / `-dx,dy`
in world units, or `^f,l`: f towards the centre of the map and l to the left of that. `cc:0^600,0` is 600 in front of the
human player's base on any map and start position.

### Conditions

Every unit must meet the condition (`any=1`: one is enough).

| condition | true when |
|---|---|
| `formation <objects> <type>` | the units have that formation |
| `shape <objects> <type> [tol=30]` | the slots the units were given are those of that formation (`PlayerAssist::layoutSlots`), facing the way the last move order of the script says (the move, or away from the group across a dragged line), and every unit is within `tol` of its slot at the destination |
| `sameshape <objects> [tol=30]` | they stand as at the snapshot, relative to their centre (keep shape) |
| `ahead <A> <B> [by=1]` | A stands further forward than B along the last move |
| `nearline <objects> <posA> <posB> [tol=30]`, `near <objects> <pos> [tol=50] [centre=1]`, `atsnapshot <objects> [tol=30]`, `apart <objects> min=<d>` | places |
| `linked`, `unlinked`, `protects <objects> <protected>`, `state <objects> home\|responding\|returning`, `athome <objects> [tol=45]`, `homeat <objects> <pos> [tol=60]` | protect links |
| `alive`, `dead`, `damaged`, `idle`, `health <objects> above\|below <percent>` | units |
| `selection <objects> [exact=1]` | the units are in the selection the script made last (`select`, `idle`); `exact=1`: nothing else is |
| `repeating <objects>` | repeat production is on for the buildings |
| `queued <building> <op><n>` | entries in the production queue of the building |
| `cash <slot> <op><n>` | the player's money |
| `count <objects> <op><n>` | the number of live objects (with `all:` the units of a template) compared with n: `=2`, `>=4`, `<=0` |

### What the real-data scenarios check

| scenario | checks |
|---|---|
| `formation-line`, `-column`, `-wedge`, `-box`, `-loose` | a click move in that formation: every unit on its slot at the destination; the tanks ahead of the rocket soldiers (column, wedge, box, loose) |
| `formation-keep` | keep shape: after a move the units stand as before, relative to each other |
| `formation-drag` | drag to aim: the line on the dragged front line, facing away; a narrow drag with the box gives rows as wide as the drag |
| `formation-hotkey-group` | a hotkey group keeps its formation when it is selected again |
| `protect-building`, `protect-unit`, `protect-group` | the link; an attacker hits the protected command center / tank / hotkey group (a member added later too); the protectors answer, go home after the fight; a move by hand moves the home; unprotect removes the link |
| `assists-not-allowed` | formation and protect orders change nothing in a match that does not allow them |
| `base-defend` | base under attack: idle army units go to the attacked command center and back to where they stood |
| `stance-retreat-heal` | four Rangers hurt to 40% (retreat at 70%) go into the heal building (`@HEAL_PAD`), come out at 100% and go back to where they stood |
| `stance-retreat-repair` | three Crusaders hurt to 35% (retreat at 50%) dock at the repair building (`@REPAIR_PAD`), are repaired to 100% and go back |
| `stance-retreat-rally` | without a repair building: three Crusaders hurt to 35% pull back to the rally point and park there; a move order of the player wins, and they pull back again only after the 15 s |
| `stance-kite`, `stance-spread`, `stance-split` | the stance's decision lines while a fight goes on |
| `repeat-production` | a Ranger is built again and again; it waits while money minus cost is below the reserve; it stops when switched off |
| `idle-select` | idle hotkeys: the next idle army unit by id (round again), never a busy one; all idle army units; the next idle worker |

`base-defend` and the `stance-*` scenarios need the commits that add MSG_ASSIST_BASE_DEFEND and MSG_ASSIST_STANCE; on a
build without them they fail with "this build has no command".

## The engine mode

```
<game> -assistMatch map=<map> players=human:<side>,idle:<side> seed=<n> steps=<step;step;...>
       [maxframes=<n>] [label=<text>] [assists=0] [cash=<n>] [debug=0]
```

It implies `-headless`, plays the logic as fast as it can, prints `ASSISTMATCH_STEP` / `ASSISTMATCH_CHECK` lines and one
`ASSISTMATCH_RESULT {json}` line (`ASSISTMATCH_ERROR` first when it cannot go on), and exits (0 when every check passed).
The `ASSIST` decision lines of `-assistDebug` are on unless `debug=0`. The skirmish setup, the match loop and the result
JSON are those of `-aiMatch` (`AIMatchShared.h`).

Files: `GeneralsMD/Code/GameEngine/Source/Common/AssistMatch.cpp` (the mode, the script language and the conditions),
`Include/Common/AssistMatch.h`, `Include/Common/AIMatchShared.h` (shared with `AIMatch.cpp`),
`Source/GameLogic/AssistTest.cpp` (`PlayerAssist::createTestUnits`, shared with `-assistTest`),
`scripts/assistbench/assistbench.mjs` (the runner; it uses `scripts/aibench/lib`).
