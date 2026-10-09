# Enhancing the Zero Hour AI

Status: first version done on branch `claude/ai-enhancement` (Expert difficulty and the
test bench), plus six unit and wave tactics (second part of "Results"); Phase 3 and further tuning
are future work. See "Results" at the end.

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

## Results (first version)

What exists:

- **Expert AI** (new slot "Expert AI" in skirmish and LAN games, `SLOT_EXPERT_AI`). It plays
  with Hard's economy, handicaps and data, so it wins only by playing better. Easy, Normal and
  Hard are the original AI. All new behaviour is gated on the Expert slot.
- **Strategy** (`AIStrategy.cpp`): army doctrine, scouting, attack waves that gather and
  regroup, retreat of damaged units, economy (gatherers, starved factories), superweapon
  targeting. **Combat model** (`AICombatModel.cpp`): weapon, armour and cost matchups for
  counter picks. **Enemy model** (`AIEnemyModel.cpp`): only what the AI has seen. Focus fire
  for Expert units (`AI.cpp`).
- Tuning knobs in an `ExpertSkill` block of `AIData.ini` (code defaults otherwise).
- **Test bench**: `-aiMatch` headless matches and `scripts/aibench` (batches, Wilson
  confidence intervals, Elo, replay determinism check). See `scripts/aibench/README.md`.
- Starter content AI fixed (it stopped after its first power plant): `TeamResourcesToStart`
  was 100, and the skirmish scripts had no build-order actions for barracks and factory.

Measured on the starter content (map Ironwood Crossing, 40 seeds, Expert vs. Hard, both
Ironwood): Expert won 26 (65%, 95% CI 50-78%), Hard won 3 (8%), 11 timeouts (not counted as
wins). Expert wins almost always from start position 1 and is about even from position 2.
Replays were identical in the determinism check.

The target of about 80% was relaxed to the current level for now. Open: the start-position
asymmetry, Phase 3 (difficulty from skill), and tuning with the retail game data (run the
bench with `--data <Zero Hour folder>` on a machine that has it).

## Results (tactics: split fire, threat targets, kiting, fight check, merge, spread)

Six behaviours were added to the Expert AI, each with a switch for the bench (`expert:Ironwood:off-<word>`; `on-<word>` for
a feature that is off by default), a tuning value in the code defaults and in the optional `ExpertSkill` block of `AIData.ini`,
and a line in the `trace` output. Easy, Normal and Hard are unchanged. Everything is a function of synchronised state (no
wall clock, no pointer-keyed iteration, arrays that are saved with the game); the enemy is only known through the enemy model
(what the player has seen). The work is spread over frames: the unit tactics look at four units per frame, round robin.

| # | feature | word | what it does | tuning (`ExpertSkill`) | code |
|---|---|---|---|---|---|
| 1 | Split fire | `split` | A unit that picks a target tells a small ledger how much damage it is about to deal (its dps against the target's armour times the window). Units that pick later see what is already assigned: a target that is (nearly) doomed gets -100, a half assigned one up to -40, so the group moves on to the next target. Entries expire after the window (the shots are on their way, then it is the target's health that counts again). | `SplitFire`, `SplitWindowSeconds` (2) | `pickTacticalTarget` in `AI.cpp`, ledger in `AITactics.cpp` |
| 2 | Threat-first targets | `threat` | The danger of a candidate is the value-weighted share of the units around the shooter (up to 8 within 150) that it destroys per second (weapon against armour, `AICombatModel::killRate`), not only what it does to the shooter. Order: threats (55-95), then healers and repairers (48; units with a healing weapon, `HealContain`/`RepairDock` modules), then workers (30), then other armed units; plus 12 for a unit that out-ranges the shooter (1.35 times) and is within reach. The old terms (finish hurt units, quick kills, in reach, near) stay. | `ThreatTargets` | `pickTacticalTarget`; support level and speeds in `AICombatModel.cpp` |
| 3 | Kiting | `kite` | A unit that is not slower than its enemy and out-ranges it by 10% (or is 1.3 times faster with about the same range) and that waits for its weapon for more than 0.7 s steps back to the edge of its range, directly away from the victim, for at most 45% of the wait; when the weapon is almost ready it attacks the same victim again, afterwards it goes on with its team's order. It does not kite when the enemy cannot hurt it (`KiteMinThreat`), when a faster enemy is about, when the spot behind it is off the map, impassable or covered by other known enemies, or when it would leave its team by more than `KiteGroupRadius`. **Off by default** (see the measurements). | `Kiting` (No), `KiteMinReloadSeconds`, `KiteRangeFactor`, `KiteSpeedFactor`, `KiteGroupRadius`, `KiteMinThreat` | `AITactics.cpp` (`planKite`, `updateSteps`) |
| 4 | Fight check | `fight` | Before a wave goes, the whole field army is weighed (Lanchester estimate as in the fight evaluation) against what the enemy model knows within 450 of the objective, including defences that cover it. If ours/theirs is below `LaunchAdvantage` the wave waits at the rally point and the army keeps growing; after `LaunchBlockSeconds` it goes anyway. While the wave is on its way (not yet at the objective) the same estimate is repeated every 2 s; below `PullbackAdvantage` for the reaction time plus 3 s, all teams return to the rally point and regroup. Once the wave is at the objective the teams weigh the fight they are in as before. | `FightCheck`, `LaunchAdvantage` (0.8), `PullbackAdvantage` (0.5), `LaunchBlockSeconds` (120) | `forecastAdvantage`, `checkWaveLaunch`, `checkWaveOnTheWay` in `AIStrategy.cpp` |
| 5 | Merge reinforcements | `merge` | A team that appears while a wave is out gathers at the rally point (it used to hunt on its own, as the scripts order). Teams that wait there go after the wave together once they add up to 30% of the wave's value (or of `MinWaveValue`), otherwise they are part of the next wave. A unit that joins a team after the team's last order (reinforcement of a team in the field) is kept out of the team's position and sent to the rally point. | `MergeReinforcements` | `evaluateTeam`, `reinforceWave` in `AIStrategy.cpp` |
| 6 | Spread out | `spread` | When the enemy model holds weapons with a blast of at least `SplashRadiusThreshold` (20; the larger of the primary and secondary radius, poison and radiation count as 40 for the cloud) for a significant share of its armed value, units keep `blast` (25..`MaxSpacing`) apart: an idle unit that stands closer than 75% of that moves away from the others; a unit that waits for its weapon steps aside to a spot that stays in range of its target, then attacks again. Units on the march keep the group's path. | `SpreadVsSplash`, `SplashRadiusThreshold` (20), `MaxSpacing` (70) | `refreshSplashThreat`, `planSpread` in `AITactics.cpp` |

Other changes: unit speed, reload time, blast radius and support level are part of `AICombatFigures`; a weapon that heals no
longer counts as a damage weapon in the combat model.

### Measurements (starter content, Ironwood Crossing, 25 game minutes, `scripts/aibench`)

Win rates are for the Expert AI against the original Hard AI (both Ironwood, start positions rotating with the seed; timeouts
are not wins). Intervals are Wilson 95%.

| configuration | seeds | Expert wins | Hard wins | timeouts |
|---|---|---|---|---|
| before the tactics (all six off: `off-split+threat+kite+fight+merge+spread`) | 1-40 | 26/40 = 65% (50-78%) | 3 | 11 |
| **final default** (kiting off) | 1-40 | **21/40 = 53% (37-67%)** | 10 | 9 |
| before the tactics | 41-120 | 46/80 = 57% (47-68%) | 14 | 20 |
| final default | 41-120 | 45/80 = 56% (45-67%) | 14 | 21 |
| before / final, 120 seeds together | 1-120 | 72/120 = 60% vs. 66/120 = 55% | | |

Replays were identical in the determinism check for every configuration that was checked (final default: 4 of 4 on seeds 1-40;
10-seed runs of each step: 2 of 2). The first 40 seeds are a lucky set for the old Expert (65% against 57% on the next 80); the
difference between old and new on all 120 seeds is within the noise (z about 0.8), but it is not a gain either. The decision of a match
often falls in one early skirmish (a single kiting step in the first two minutes changed the whole match in a trace), so
differences below about 15 points need more than 100 seeds.

Start positions (Expert wins / Hard wins / timeouts, by the Expert's start): before, seeds 1-40: start 1 20/0/0, start 2 6/3/11;
final, seeds 1-40: start 1 18/0/2, start 2 3/10/7; before, seeds 41-120: start 1 36/0/4, start 2 10/14/16; final, seeds 41-120:
start 1 39/0/1, start 2 6/14/20. As before, the Expert wins nearly always from position 1 and is the weaker side from position 2.

One feature alone against Hard (all other new words off):

| feature on | seeds | Expert wins | Hard wins |
|---|---|---|---|
| split | 1-40 | 24/40 = 60% | 5 |
| threat | 1-40 | 26/40 = 65% | 5 |
| kite (first version) | 1-40 | 19/40 = 48% | 9 |
| kite (with `KiteMinThreat`) | 1-40 / 41-120 | 19/40 = 48% / 37/80 = 46% | 7 / 13 |
| merge, new teams only waiting for the next wave | 1-40 | 9/40 = 23% (27 timeouts) | 4 |
| merge with the follow-up group | 1-40 / 41-120 | 22/40 = 55% / 48/80 = 60% | 3 / 14 |

The first version of merge held the new teams until the next wave, which only starts when the wave is spent, and
the AI could no longer finish a base (23%); the follow-up group fixed that. The "before" row for comparison: 65% and 57%.

Per feature, mirror matches (Expert against Expert that has the word off; wins of each side, from the 60 seeds of each; the start
position bias of the map is large in mirror matches, which makes them noisy):

| feature | with | without | timeouts |
|---|---|---|---|
| split | 21 | 24 | 15 |
| threat | 27 | 27 | 6 |
| merge | 26 | 30 | 4 |
| kite (`on-kite` against the default) | 21 | 27 | 12 |
| kite, first version (default then on), 120 seeds | 44 | 53 | 23 |
| spread, overlay `splash` (rocketeers are artillery with blast 35/60), 40 seeds | 24 | 13 | 3 |
| spread, same overlay, earlier run, 20 seeds | 7 | 4 | 9 |
| fight check, overlay `towers` (gun towers), 40 seeds | 1 | 2 | 37 |

Reading: none of the changes shows a measurable gain on the starter content (four units, no healers, no defences, no artillery) except
spreading out against artillery (65% of the decided matches, 31 against 17 over both runs). Kiting is slightly negative in
every comparison (about 45% of the decided matches), so it is off by default; it is meant for factions with fast ranged units and
needs tuning with real data. The fight check never held a wave on the plain starter data (the enemy base is unseen when the
wave leaves), so it is exercised with overlays only: `towers` + `strictfight` produced holds, a forced launch and pullbacks in the trace;
with the towers alone the matches are almost all timeouts (37 of 40) and cannot show an effect.

What could not be tested on starter data: support targets (there is no healer; only workers count as support), the "out-ranging unit"
bonus (it fired a handful of times), reinforcements joining a team in the field (the starter teams do not reinforce), poison and
radiation as blast sources, and kiting against faster enemies. They follow the same code paths and are covered by the trace, not by a bench result.

Bench additions: `--keep-logs`, `--overlay` with the fixtures `towers`, `strictfight` and `splash` (`scripts/aibench/fixtures/`),
`on-`/`off-` words in the player variant. See `scripts/aibench/README.md`.
