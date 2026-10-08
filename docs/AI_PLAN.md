# Enhancing the Zero Hour AI

Status: in progress on branch `claude/ai-enhancement`.

## Scope and constraints

- Improve the skirmish/computer-player AI of Zero Hour in the engine.
- Compatibility with replays and multiplayer games of earlier builds is **not**
  required (the web port has not been released). The AI may change directly.
- The simulation must stay **deterministic**: every client and every replay of
  the *same* build must compute identical results. AI code may only use
  synchronised state and `GameLogicRandom*`, never wall-clock time, client
  random numbers, uninitialised memory or iteration over pointer-keyed
  containers.
- **No external API calls**: everything runs locally inside the game.
- The original game data is EA's and is not in the repository. Engine
  behaviour must work with any data set; tuning knobs live in code defaults and
  an optional INI file we author (no edited copies of EA files). Mechanics are
  developed and tested on the free starter content; tuning for the real
  factions is done with the test bench on a machine that has the game.

## Where the AI lives

| Layer | Code | Role |
|---|---|---|
| Strategic | `GeneralsMD/Code/GameEngine/Source/GameLogic/AI/AIPlayer.cpp`, `AISkirmishPlayer.cpp` | Base building, supply, team training, upgrades, skill sets, superweapons, base defence, repairs |
| Data | `AIData.ini` (`SkirmishBuildList`, `SkillSet*`, build and team timers, resource gatherers per difficulty, guard and retaliation radii, group sizes) | Per-side behaviour knobs |
| Scripts | `SkirmishScripts.scb` | Teams to build, attack waves |
| Tactical | `AIStates.cpp`, `AIGroup.cpp`, `Squad.cpp`, `AIGuard*.cpp`, `TurretAI.cpp` | Unit and group behaviour, targeting, guarding |

Typical weaknesses: fixed build orders, no scouting, no adaptation to the
enemy army, units fight to the death, weak use of generals' powers,
difficulty driven by economy multipliers rather than skill.

## Phase 0: AI-vs-AI test bench

- A headless match mode, e.g.
  `-aiMatch map=<map> players=<difficulty>:<side>,... seed=<n> timeout=<min> stats=<file.json>`,
  running at maximum speed without rendering (existing `-headless` mode).
- JSON statistics per match: winner, duration, money gathered and spent,
  units and structures built and lost by type, army value over time, idle
  production time, supply-source control.
- A batch runner (several seeds, maps and matchups in parallel): win-rate
  matrix, Elo per AI configuration, and a determinism check that replays each
  match and compares CRCs.
- Runs natively and in the web build; usable with the starter content here and
  with the real game data elsewhere.

## Phase 1: strategic improvements

- Scouting and an enemy model (composition by role: infantry, vehicles,
  aircraft, defences).
- Counter-building: team selection weighted by what beats the observed enemy
  composition.
- Economy: supply-source expansion and protection, gatherer counts by game
  phase, avoiding idle production.
- Better build orders and tech timing; sensible skill-set and general's power
  use; superweapon targeting by value.
- Attack waves that gather, attack a chosen objective and regroup.

## Phase 2: tactical improvements

- Retreat and repair damaged units; avoid suicide into static defences.
- Focus fire and target priority (threat, value, health).
- Kiting for fast ranged units; keep artillery behind the front line.
- Use unit abilities and garrisons.

## Phase 3: difficulty from skill

- Difficulty levels shaped by reaction delay, attention (actions per minute),
  scouting frequency and mistake rate instead of resource bonuses.
- Optional adaptive difficulty that follows the player's performance.

## Measuring success

- Each change is measured on the test bench against the previous AI.
- Targets: the improved Hard AI wins at least 65-70% of matches against the
  original Hard AI over many seeds; zero determinism failures; AI update cost
  per logic frame stays within budget in the web build.
