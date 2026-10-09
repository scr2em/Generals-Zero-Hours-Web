# AI test bench report

Generated 2026-10-09T02:51:16.434Z. Data: starter. Maps: ironwood_crossing. Seeds per matchup: 40 (from 1). Time limit 25 game minutes. Start positions rotate with the seed.

## Summary

```
== candidate: 40 matches: 29 victories, 11 timeouts, 0 draws, 0 errors
   expert:Ironwood vs hard:Ironwood: expert:Ironwood 26/40 wins (65%, 95% CI 50%-78%), hard:Ironwood 3/40 (8%, 95% CI 3%-20%), 11 timeouts, 0 draws
   Elo: expert:Ironwood 1112, hard:Ironwood 888
   speed: 5639 logic frames/s on average (2374 at the slowest; real time is 30)
== determinism: 4/4 replayed matches identical
```

## Build: candidate

40 matches: 29 victories, 11 timeouts (no winner, never counted as a win), 0 draws, 0 errors.

### Head to head

| matchup | first wins | 95% CI | second wins | timeouts | draws | median length |
|---|---|---|---|---|---|---|
| expert:Ironwood vs hard:Ironwood | 26/40 (65%) | 50%-78% | 3/40 (8%) | 11 | 0 | 11.6 min |

### Win-rate matrix

Row configuration against column configuration: wins / games (timeouts and draws are games without a win).

| | expert:Ironwood | hard:Ironwood |
|---|---|---|
| **expert:Ironwood** |  | 26/40 (11 no result) |
| **hard:Ironwood** | 3/40 (11 no result) |  |

### Elo

Maximum likelihood over all games, 1000 = a phantom opponent every configuration drew once against. Timeouts and draws count as half a win. Few games give wide error bars: read it together with the intervals above.

| configuration | Elo | games |
|---|---|---|
| expert:Ironwood | 1112 | 40 |
| hard:Ironwood | 888 | 40 |

### Start position bias

Victories by start position over decided matches; a big difference points at the map, not at the AI.

| start | wins / games |
|---|---|
| 1 | 23/29 |
| 2 | 6/29 |

### Averages per configuration

| configuration | games | money gathered | money spent | peak army value | final army value | idle production | units built | units lost | objects destroyed |
|---|---|---|---|---|---|---|---|---|---|
| expert:Ironwood | 40 | 8,082 | 12,629 | 4,949 | 3,079 | 85% | 51.9 | 41.2 | 62.1 |
| hard:Ironwood | 40 | 8,965 | 13,866 | 3,433 | 413 | 83% | 63.2 | 59.5 | 42 |

### Speed

40 matches: 5,639 logic frames per second on average (median 5,319, slowest 2,374; the game runs at 30), mean 27,652 frames per match, map load and match setup 2.9 s, 11.6 s per match including the browser.

## Determinism

4/4 replayed matches reproduced the same CRC at every sample (and the same statistics).

| match | frames | CRC samples | result | divergence |
|---|---|---|---|---|
| candidate-ironwood_crossing-m1-s1 | 14,717 | 50 | identical |  |
| candidate-ironwood_crossing-m1-s19 | 14,010 | 47 | identical |  |
| candidate-ironwood_crossing-m1-s28 | 45,000 | 150 | identical |  |
| candidate-ironwood_crossing-m1-s37 | 17,295 | 58 | identical |  |

