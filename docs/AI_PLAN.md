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

The target of about 80% was relaxed to the current level for now. Open: Phase 3 (difficulty from skill), and tuning with the retail game data (run the
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

## Results (batch 2: base defence, terrain, economy, garrisons, abilities)

Ten more behaviours, again each with a switch for the bench (`expert:Ironwood:off-<word>`; `on-<word>` for a feature that is off by
default), tuning values in the code defaults and in the optional `ExpertSkill` block of `AIData.ini`, a line in the `trace` output, and
state that is saved with the game (`AIStrategy::xfer` version 12). Easy, Normal and Hard are unchanged. Everything uses synchronised
state only (frame counters, the pathfinder, `GameLogicRandom`; no wall clock, no pointer-keyed iteration), the enemy through the enemy model
(what the player has seen) and the start positions of the map (public in a skirmish), and generic data of templates and modules (KindOf,
weapons, contain modules, special powers, locomotors, command sets): no unit names. The work is spread over frames (the largest single
job, the ways into the base, is one pass of a few pathfinder queries).

### Base defence has priority, and terrain-aware defence (asked for by users)

**Base defence priority** (`basedef`, `AIBaseDefence.cpp`). The user's report: the Expert kept its units waiting at the rally point while the
base was being attacked (the holds of the fight check and of the merge, added in the tactics batch, sat on top of the engine's defence).
Reproduced with the trace before the change (`expert:Ironwood:trace+off-basedef` against Hard, the alarm line is printed with the response off):

```
BASEDEF alarm 3: enemy force 600 at (729,820) in the base zone; 4 of 4 units near the rally point stand idle; army state gather; response OFF
```

A threat is armed enemy units seen inside the base zone (the base radius plus `BaseDefenceMargin` 120) worth at least `BaseDefenceMinValue`
(150), or damage by an enemy to one of our objects inside it (`ActiveBody` reports it through `Player::aiObjectDamaged`). While there is one:
the teams that are at home (gathering, held by the fight check, merged reinforcements, a wave that has not got far) go for it together
(attack-move; `sendTeamToBase`), they are not sent back to the rally point, no wave is launched and raiders come home, and when nothing
was seen in the zone for `BaseDefenceClearSeconds` (5) they go back to their role. A threat that is much stronger than the force at home
(`BaseDefenceMinAdvantage` 0.6) and is not hitting anything of ours does not draw the teams out one by one: they stay together at the rally
point. A wave that is deep in enemy territory only turns round when the fight check says so (the combat model), as before.
First version (zone +250, threshold 100, one team per order): 15/40 wins and 23 timeouts, the teams chased single scouts at the edge of the
zone; the tuned version: 23/40 (57%, 95% CI 42-71%), Hard 1 win, 16 timeouts, against the same Expert with `off-basedef` 22/40 (55%, 40-69%),
Hard 9 wins, 9 timeouts (seeds 1-40, determinism 4/4): the same win rate, and the Expert no longer loses games to early attacks.

**Terrain-aware defence** (`geo`, `layout`; `AIGeo.cpp`). `geo`: the ways into the base come from the pathfinder. The ground reachable from the
base is flooded (`groundOpen` = the pathfinder cell is passable); if a ring around the base is mostly closed by terrain (cliffs, water,
buildings), the gaps in it are the ways in, every gap that the enemy start can reach by a ground path counts, and its share of the expected
attacks falls with the length of that path (`(shortest/length)^3`); on open ground the crossing of the path from each enemy start with the
perimeter is the way in. On each way in the path is walked outwards and the passable span across it is measured every 10 units: the
narrowest place (at most `GeoChokeWidth` 200 and under 75% of the widest) or a bridge is the chokepoint. The share of a way in is mixed
with what the enemy model has seen of enemy ground forces near it (up to half). Uses: (1) defence structures (a template with KindOf
FS_BASE_DEFENSE that hits ground units) of the build list that are not built yet, and the ones the script action "build base defence" asks for,
are put where they cover the chokepoint of the way in with the fewest defences for its share (candidates around the chokepoint at 40-80%
of the weapon range, within the base radius plus `GeoReach` of the base, reachable on foot from the way in, legal to build, away from other
defences; higher ground and the inner side of the chokepoint score higher); (2) the rally point is on the main way in, beside the
chokepoint (`GeoRallyOffset` outside it; on open ground `GeoRallyOut` outside the base radius, on the path itself, not on the straight
line to the enemy start). Waiting inside the gap was tried first and was a bad idea (the army blocks its own way out and the enemy goes
round to the other gaps): 3 wins against 38 for the default rally point on the map with gates. `layout`: the original code turns the
build list of every start position by the same 135 degrees (`RotateSkirmishBases = No`), which is right for one start of a two player
map and turns the base the wrong way round at the other. The Expert turns the layout to face the enemy start(s) (direction to them plus 90
degrees, which is the original turn for the first start). Trace of the ways in on the map with gates (`--overlay gates,geotowers --map ironwood_gates`):

```
GEO: terrain closes the perimeter around the base (radius 173, perimeter 293): 3 way(s) in
GEO: way in 1: crosses at (468,466) bearing 42 deg; chokepoint at (480,470) width 100; expected 52% of the attacks; army waits at (440,441)
GEO: way in 2: crosses at (64,493) bearing 130 deg; chokepoint at (60,500) width 51; expected 24% of the attacks; army waits at (91,461)
GEO: way in 3: crosses at (476,80) bearing -40 deg; chokepoint at (476,80) width 51; expected 24% of the attacks; ...
GEO: defence IronwoodGeoTower (range 200) at (424,414) height 16 for way in 1 (chokepoint at (480,470), 1 defence(s) there now)
GEO: build list IronwoodGeoTower moved from (174,386) to (424,414)
```

(the fourth gap, behind the base, leads into a dead end and is dropped). Fixtures: `gates` (a bench-only map with cliff-rimmed basins, generated by
`scripts/aibench/fixtures/maps/gates.py`) and `geotowers` (three towers on the build list, built by the skirmish build code).

Start positions, Expert against Hard, seeds 1-40 (Expert's wins / losses / timeouts, by the Expert's start position):

| configuration | start 1 | start 2 |
|---|---|---|
| before base defence (`off-basedef`) | 18 / 0 / 2 | 4 / 9 / 7 |
| base defence | 17 / 0 / 3 | 6 / 1 / 13 |
| base defence + `geo` + `layout` (final) | 17 / 0 / 3 | 7 / 0 / 13 |

and Expert against Expert (`on-layout` against the default, 40 seeds in each order; wins / losses / timeouts of the player by his start): default at start 1
6 / 3 / 31, default at start 2 1 / 30 / 9; layout at start 1 30 / 1 / 9, layout at start 2 3 / 6 / 31. So the layout was the cause of the
weakness at the second start: with it the Expert at the second start no longer loses (9 lost games became 0 against Hard, and the mirror
match is even), but those games now end in timeouts: the Hard AI at the first start still has the layout that was made for it and holds its
position (the timeouts are mostly stalemates, as between two equal Experts). The Hard AI keeps the old layout at both starts (the change is
Expert only), so it is still the weaker side at the second start.

A note on the method: in a mirror match (Expert against Expert) the player in the first slot is favoured or not by the engine's player order, so every A/B was played in both slot orders and the wins added (a null test with two identical configurations gave 7 against 13 in one order). The report of the bench lists the wins by start position (`startBias`).

Per feature (head to head against the default Expert; each row from both slot orders, wins of the feature / wins of the default):

| feature | overlay, map | wins on / off | timeouts |
|---|---|---|---|
| `geo` | none, Ironwood Crossing | 28 / 29 | 23 of 80 |
| `geo` | `geotowers`, Crossing | 13 / 5 | 62 of 80 |
| `geo` | `gates,geotowers`, Gates | 14 / 7 | 59 of 80 |
| `geo` sites only | `gates,geotowers`, Gates | 12 / 10 | 58 of 80 |
| `geo` rally inside the gap (rejected) | `gates,geotowers`, Gates | 3 / 38 | 39 of 80 |
| `layout` | none, Crossing | 33 / 7 | 40 of 80 |

### The other features

| # | feature | word | what it does | tuning (`ExpertSkill`) | code |
|---|---|---|---|---|---|
| 7 | Economic raids | `raid` | A party of up to `RaidUnits` (3) of the fastest armed units of the field teams (at least `RaidSpeedFactor` = 1.25 times the army's average speed, healthy, teams that are not on a wave or retreating, weapons that hurt the target by their damage against its armour) goes after a gatherer the enemy model has seen (units of the economy role that are not structures: supply trucks, harvesters, workers). The target must be safe: no known ground defence covers it, and the armed units seen around it or on the way to it are worth less than `RaidGuardShare` (0.3) of the party. The party attacks the target while it is in sight, goes to where it was seen when it is not, takes the next gatherer in reach when it is down, and comes home when the fight check (weighed twice a second around the party) says the defenders are too strong (`RaidPullbackAdvantage` 0.9), when it is hurt (under 45% health), when `RaidMaxSeconds` (75) are over, or when no gatherer is in reach. While out, the party is detached from its teams (team orders skip it, the tactics skip it). | `RaidEconomy`, `RaidUnits`, `RaidSpeedFactor`, `RaidStartSeconds` (120), `RaidMaxSeconds`, `RaidCooldownSeconds` (30), `RaidPullbackAdvantage`, `RaidGuardShare` | `AIRaid.cpp` |
| 8 | Defend workers | `protect` | A protect relation (`AIProtect.h/.cpp`, independent of the strategic AI): protectors are assigned to protected objects (`assign`); the damage code reports a hit on a protected object by an enemy (`AIProtectNotifyDamage` from `ActiveBody`); the nearest suitable protectors (armed, able to hurt the attacker, not losing the exchange badly) go for the attacker, or to the place when it is out of sight, enough of them for the armed value seen around the object (at most `ProtectResponders`); they stay within the leash of their post (`ProtectLeashRadius`), take the next enemy when the attacker is down, and go home when things are calm for `ProtectCalmSeconds`, when they have strayed too far, or after `ProtectMaxSeconds`. A human Protect command can use the same interface (`assign`, `releaseProtector`, `update`, the damage hook). The Expert AI protects its gatherers and workers (KindOf HARVESTER or DOZER) with the armed units of the teams that are at home, refreshed every two seconds. | `ProtectWorkers`, `ProtectLeashRadius` (450), `ProtectResponseRadius` (500), `ProtectCalmSeconds` (6), `ProtectMaxSeconds` (75), `ProtectResponders` (4) | `AIProtect.h/.cpp`, `updateProtection` in `AITactics.cpp`, hook in `ActiveBody.cpp` |
| 9 | Repair and heal | `repair` | A damaged vehicle (below `RepairBelow` of its health, nothing armed of the enemy within 260) goes to the nearest repair pad of ours (an object with KindOf REPAIR_PAD, e.g. a war factory with a RepairDockUpdate), a damaged infantryman to a heal pad (HEAL_PAD: a HealContain building or an ambulance-like unit); the action manager (`canGetRepairedAt`, `canGetHealedAt`) says whether the unit may use the pad, trips are limited to `RepairTripSeconds` at the unit's speed and two visitors per pad. The unit is detached from its team on the trip and while it is mended, and returns to its team (or the wave's objective) afterwards. Idle dozers repair damaged structures (`RepairDozerBelow`) that are farther away than their own bored-repair range. | `RepairAndHeal`, `RepairBelow` (55%), `RepairTripSeconds` (30), `RepairDozerBelow` (85%) | `AIRepair.cpp` |
| 10 | Avoid static defences | `route` | The reach of every defence in the enemy model (an armed structure that shoots ground units; its weapon range plus `RouteMargin`) is a circle to stay out of. Before a wave goes, `AIPlanDetour` (`AIRoute.cpp`, a shortest path over a small visibility graph: eight points around every circle) plans up to three waypoints from the army to the objective; the wave moves from point to point and gathers at each one (a team that stands at the current waypoint waits for the others, `routeHolds`; the wave goes on when 70% of its value is there or after 15 s). A way more than `RouteMaxDetour` times the straight way is not taken. Defences that cover the objective itself cannot be avoided: units whose weapon out-ranges the longest of them by `BreachRangeFactor` and that can hurt structures (up to six, the longest range first) become breachers: they go to a firing spot at the edge of their range, outside the reach of the other defences, and shoot the defences while the wave waits at a staging point outside the reach (`BreachHoldSeconds`). Enemy troops that come for the breachers send them back to the wave until it is quiet. The wave goes in when the defences are down, the breachers are lost, or the time is up. | `AvoidDefences`, `RouteMargin` (60), `RouteMaxDetour` (1.8), `BreachRangeFactor` (1.12), `BreachHoldSeconds` (80) | `AIRoute.h/.cpp`, hooks in `updateArmy`, `evaluateTeam` |
| 11 | Garrisons | `garrison` | Defence: armed enemy ground units worth `GarrisonThreatValue` (300) in the base zone make the infantry that are at home (teams that are not on a wave or retreating, near the base) enter the structures of the base whose contain module is a garrison (`isGarrisonable`; the standing posts of the bunker feature are left to that feature) and that are within 450 of the attackers, as many as there are free places, but not when the attackers out-range the infantry by more than 1.3 times (a garrison could not answer) (`canEnterObject` asks the action manager); they leave `GarrisonHoldSeconds` (8) after the last enemy was seen, and go back to their teams. Attack: a visible enemy structure with a garrison and occupants does not shoot back at units that ignore it, so (only when the fight advantage there is at least 1.0) the (up to four) units with the best damage per second against it (`AICombatModel::damagePerSecond` against its armour) within 600 of it attack it until it is empty or down, or 45 s are over. | `UseGarrisons`, `GarrisonThreatValue`, `GarrisonHoldSeconds`, `GarrisonClear` (word `clear`) | `AIGarrison.cpp` |
| 12 | Unit abilities | `ability` | A unit of a field team looks (a few units per frame, round robin) at the buttons of its command set: a SPECIAL_POWER button that asks for a target (`NEED_TARGET_ENEMY_OBJECT`, `NEED_TARGET_ALLY_OBJECT`, `NEED_TARGET_POS`) whose power module is ready and whose science the player has (`SpecialPowerStore::canUseSpecialPower`). The target must be valid for that power (`CommandButton::isValidToUseOn`, which asks the action manager and so applies the rules of that power's type) and in sight: for an object power the most valuable enemy within `AbilityRange` (cost, armed things first, structures count half), or the most hurt friend (below 80% health); for a position power the center of the most enemy value inside the power's radius with little of ours (friends count three times against). Targets worth less than `AbilityMinValue` are left. A unit that used a power is left alone for 6 s (it may walk to the target first). Powers without a target (deploy, stealth) are not touched. | `UnitAbilities`, `AbilityRange` (400), `AbilityMinValue` (150) | `AIAbility.cpp` |
| 13 | Airborne insertion | `airborne` (**off by default**) | When the player owns a transport aircraft (KindOf AIRCRAFT with a contain module for passengers; it is kept out of the army's orders and does not stop its team from being managed) and a squad of at least two armed infantry or light units worth `AirMinSquadValue` that may board (`canEnterObject`) is at the rally point, it looks for a soft target in the enemy model: a superweapon structure (weight 2.5), power (1.4), production (1.2), gatherer (1.0), nearer is better. The drop point is one of eight points 110 units from the target that no known ground defence covers (armed units seen near it must be worth less than `AirGuardShare` of the squad) and that is outside every anti-air area (a seen weapon that can hit airborne units: unit or structure, range plus `AirMargin`). The way out and the way back are planned around those areas with `AIPlanDetour`; if there is no way (or none within `AirMaxDetour`) the aircraft stays home (trace: "no safe way"). The aircraft flies to the rally point, the squad boards, it flies the waypoints, evacuates at the drop point, the squad attacks the target (`AirAssaultSeconds`), and the aircraft returns the planned way. | `AirborneInsertion`, `AirMargin` (50), `AirGuardShare` (0.8), `AirMinSquadValue` (350), `AirMaxDetour` (2.2), `AirMaxSeconds` (100), `AirAssaultSeconds` (90), `AirCooldownSeconds` (45) | `AIAirborne.cpp` |
| 14 | Standing garrisons | `bunker` | A defensive structure of ours (finished, not a clinic, tunnel or zero-slot container, with places, passengers allowed to fire, and either KindOf FS_BASE_DEFENSE, or armed, or not a production, power or other economic structure) is a post, whatever its contain module (a garrison, or a bunker: a `TransportContain` on a structure with `PassengersAllowedToFire`). It is filled and kept filled: every two seconds the posts are looked at, the one on the side of the expected attacks first (the ways in of `geo`, else near the rally point); the infantry at home that belong to no wave (units of teams that are not on a wave and units without a team) go in (`canEnterObject` decides), the mix follows what the enemy model has seen (anti-armour share = enemy vehicle value over vehicles plus infantry, 25-75%; a unit is anti-armour when its damage per second against the biggest seen enemy vehicle is at least 0.6 of that against the biggest seen enemy infantry; without sightings by the weapons' damage per shot); a man who goes in leaves his team (the scripts replace him), is detached from the waves, raids and regroups, and stays; when none is free the player trains infantry at a barracks-like structure (from its command set, the unit that suits the missing class best, money left at least `BunkerReserve` after it, at most two on order). The men in the posts are at most `BunkerArmyShare` (0.3) of the army (always two). Losses are replaced, a rebuilt structure is filled again. | `FillBunkers`, `BunkerReserve` (200), `BunkerArmyShare` (0.3) | `AIBunker.cpp` |

Defaults: all on except `airborne`. Measured, head to head against the default Expert, each from both slot orders (wins of the feature / wins of the default, timeouts
not counted as wins; the mirror matches of this map are mostly stalemates, so the numbers are small), with the overlay that lets the feature trigger:

| feature | overlay | wins on / off | result |
|---|---|---|---|
| raids | `openmap,haulers,raiders` | 20 / 19 | neutral |
| protect | `haulers` | 8 / 6 | neutral |
| repair | `repairpads` | 2 / 6 | within the noise (72 of 80 timeouts) |
| route (breach, defences that out-range the army) | `openmap,towers,longrockets` | 6 / 5 | neutral (49 timeouts); the first version was 4 / 10 and got the skip rule and the firing spots |
| route (defences on the way) | `openmap,outposts` | 24 / 14 | positive |
| garrisons | `openmap,garrison` | 0 / 4 | neutral (76 timeouts); the first version, that garrisoned against attackers that out-range infantry, was 0 / 17 |
| abilities | `ability` | 60 / 0 | the defector power of the fixture wins every game |
| airborne | `openmap,airlift` | 7 / 22 | **negative**: the squad is lost with the helicopter; off by default |
| bunker | `bunker` | 4 / 2 | slightly positive: units built 94 against 97, lost 77 against 86, killed 87 against 78, army at the end 2781 against 1625. The first version took the men out of their team members (the scripts did not replace them: units built 64 against 85) and was 8 / 17 |

On the plain starter content (no overlay) the features of the table do not trigger, and the final default is: seeds 1-40 24/40 = 60% (45-74%), Hard 0 wins, 16 timeouts; seeds 41-120 45/80 = 56% (45-67%), Hard 0 wins, 35 timeouts; all 120 seeds 69/120 = 57.5% against 66/120 = 55% for the previous final default (and 72/120 = 60% before the tactics batch), Hard won 0 of 120 (24 before). By start position (wins / losses / timeouts of the Expert): seeds 1-40 start 1 17 / 0 / 3, start 2 7 / 0 / 13; seeds 41-120 start 1 39 / 0 / 1, start 2 6 / 0 / 34 (before: 6 / 14 / 20).
Replays were identical in the determinism checks (40 seeds: 4 of 4; 80 seeds: 2 of 2; the 10-seed runs after each feature: 2 of 2). What could not be tested here: airborne insertion against anti-air in the sky of a real map (the fixture's
anti-air `antiair` is only a trace check), the script action "build base defence" (the starter scripts never call it; the hook is the same placement function as for the
build list), bridges and ramps as chokepoints (only the code path; the bench maps have none), abilities other than the defector kind, and everything with the retail game data.

## Team games (a user report: Expert allies did not attack)

The report (retail data, a 27 minute game with 50,000 cash: the human and two Expert allies against two Expert enemies): the human did all the
attacking, the Expert allies none. Team play had not been tested: the bench had only two start positions. The overlays `teams` (the map
Ironwood Teams, six starts in two team areas, 2240 x 2240 units, made by `fixtures/maps/teams.py`) and `sprawl` (eight more build list
entries, so that the base radius is about 430 as in a real game) and `--engine-arg cash=50000` are quick development checks only; the starter pack
says nothing about gameplay (CLAUDE.md), so the real-data checks are given at the end.

What the traces showed (`teams,sprawl,openmap`, four Experts, `--starts 1,2,5,6 --no-rotate`, the status line `waves: army A of N needed ...` is new):

* The size of the army that a wave needs (`waveTarget`: 1.5 times the enemy army seen plus 0.8 times the defences, times `WaveSizeScale`) counted
  every enemy at once, and with shared vision everything the allies saw, but only the Expert's own army was set against it. In a 2 against 2
  with full vision the trace reads `waves: army 12000 of 39600 needed (enemy seen 24000, allies 12000)`: a wave needs 3.3 times the army of
  the player, who sits on 48,000 cash for the whole game and never launches (45,000 frames, no `WAVE launches` line). The same holds in a
  1 against 1 with full vision (the Experts see each other's armies and neither moves); the second enemy and the allies' army that does
  the fighting just make it worse in a team game. This was not caused by the base defence of the earlier snapshot: with each of the
  new words off in turn (`off-basedef`, `off-geo`, `off-layout`, `off-bunker` ...) the stall stayed.
* The ways into the base (`geo`) were wrong for a large base: the flooded window was +-560 units, the ring around a base of radius 434 lies at
  494 and more, so the part of the ring outside the window counted as closed by terrain (`terrain closes the perimeter ... 1 way(s) in`, army waiting on
  the wrong side). The window is now +-1250 units and ground beyond it is not taken for a wall (`open ground around the base ... 2 way(s) in`,
  one per enemy start).
* The base alarm can hold the waves for long (a harassing scout or a force that stands about near the base: alarms of 217 s in a 1 against 1 with the
  bunker overlay). Now the alarm is stale after `BaseDefenceMaxBlockSeconds` (60) if nothing of ours was hit for 15 s: the teams go back to their role,
  the same force is ignored for 90 s unless it hits something (`BASEDEF clear after N s ... (stale: ...)`), and a small force (a third of the army) that has not
  hit anything for 8 s does not hold the waves back (`baseAlarmBlocksWaves`).

The fix for the team games is in `waveTarget`: the armed units of the allies (`AIEnemyModel::allyValue`, summed in the sweep over the objects) take
`AllyWaveWeight` (0.6) of their value off the army that the wave needs, and off the army that an army that has stopped growing launches at
(0.6 of the target), but not below `AllyWaveFloor` (0.4) of it (word `team`, `TeamWaves`). With no allies nothing changes.

Measured on the bench (team 0 = the two Experts with the new behaviour, `off-team+bdcap` for the old; team 1: Expert with the old behaviour and an Easy; `openmap`, 6 seeds each):

| setup | wave size / alarm cap | first wave of the Experts | victories (mean frames) |
|---|---|---|---|
| 2 Experts against Expert + Easy, full vision | old | frame 15,600 - 29,500 | 6 of 6 (27,463) |
| | new | frame 6,800 - 9,200 | 6 of 6 (15,722) |
| the same without full vision | old / new | frame 3,800 - 5,500 | 6 of 6 (17,703 / 17,596) |
| Expert + Easy (the passive ally) against 2 Experts, full vision | old | no wave in 6 games | 1 of 6, 5 timeouts |
| | new | waves in 3 of 6 games | 2 of 6, 4 timeouts |
| 2 Experts against 2 Experts, full vision (equal, symmetrical) | old / new | no wave | timeouts (equal armies do not attack each other by design) |

Plain Expert against Hard on Ironwood Crossing, seeds 1-40, after all the changes: 24/40 = 60% (CI 45-74%), Hard won 0, 16 timeouts, determinism 4 of 4, the same as before (no allies and no stale alarms in these games).

Expert allies do not guard the base of the human ally; the fight check counts allied units around the objective, and the wave relief above lets the allies
join the fronts of the human. Sending teams to an allied base under attack (damage reports for the objects of the allies) is not done: it needs a hook
for the victims of other players and could hold the army at the wrong base; left for later.

Real-data checks (Mac, `ZH_PATH`): map `hostile dawn`, four Experts, `--matchup "expert:China:trace@0,expert:China:trace@0,expert:China:trace@1,expert:China:trace@1"
--starts 1,2,4,6 --no-rotate --engine-arg cash=50000 --timeout 27 --keep-logs` (three allies, starts 1-3 against 4 and 6, with `--starts 1,2,3,4,6`, is the user's game),
and the same on `tournamenta` (4 starts: `--starts 1,2,3,4`). In the logs of every Expert: `WAVE launches` appears (a pass: at least one in the first 11 minutes, about 20,000
frames); `waves: army A of N needed` shows `allies` above 0 once the allies have an army and N below the value without the allies;
`BASEDEF clear after` seconds summed stay below a quarter of the game and an alarm of more than 90 s ends with `(stale:`; `GEO: ... way(s) in` for a large base does not say
`terrain closes the perimeter` on open ground. The old behaviour for comparison: `expert:China:trace+off-team+bdcap@0`.
