# aitune: automated tuning of the Expert AI

`aitune.mjs` searches the settings of the `ExpertSkill` block (`space.json`) for the Expert AI. It plays Expert against
Hard of the same side (America, China and GLA) on 2-player maps with the real game data, on the player's own computer.
Each match is one run of `scripts/aibench` with the native headless build.

The engine must read `skill=<field>:<value>` (`AI::setExpertSkillValue`, set by `AIMatch.cpp`).

## Use

```
export ZH_PATH="/path/to/Command and Conquer Generals Zero Hour" GENERALS_PATH="/path/to/Command and Conquer Generals"
node scripts/aitune/aitune.mjs tune --native build/native-headless --out aitune-out/run1 --workers 7
node scripts/aitune/aitune.mjs verify --native build/native-headless --out aitune-out/run1/verify \
     --params aitune-out/run1/best.json --seeds 20
```

* `tune` copies `zh_headless` into the output folder at the start. A rebuild during the run does not change the
  program under test. Run the same command again to continue a stopped run (`state.json`). Matches that are already
  played are not played again.
* Maps: `--map` (repeatable), default `tournament desert`. A map with room for more than 2 players is refused.
* `progress.md` has one line per generation. `best.ini` is the current recommendation as an `ExpertSkill` block for
  `AIData.ini` (only the settings that differ from the code defaults). `best.json` is the same for `verify`.

## Method

* Every setting is mapped to [0, 1] (log scale where `space.json` says `log`). A yes/no setting is yes above 0.5.
* Each generation plays the current mean and `--population` candidates around it (antithetic pairs: mean + step and
  mean - step). All of them play the same seeds (common random numbers), and the seeds change in each generation.
  The new mean is the weighted mean of the better half. The step size goes down by `--sigma-decay` per generation.
* Score of one match: a win 1, a loss 0, a timeout 0.1 to 0.9 by the Expert's share of the army and structure value
  left on the map. On Tournament Desert almost every match is a timeout, so only a win/loss score gives no signal.
  Money in the bank does not count.
* Elo against Hard (Hard = 1000) uses the plain score (a timeout is half a game): 1200 = an expected score of 76%.
* The recommendation is the mean of the search, not the best single candidate (that one is often only lucky).
  Always check it with `verify` on fresh seeds before you put it in the code defaults.

## Cost

Default per generation: 7 candidates x 3 sides x 4 seeds = 84 matches. A 30-minute match takes about 50 s of real time
on one core. With 7 workers, one generation takes about 10 minutes, and 20 generations take about 3.5 hours.
`verify` with 20 seeds plays 120 matches.
