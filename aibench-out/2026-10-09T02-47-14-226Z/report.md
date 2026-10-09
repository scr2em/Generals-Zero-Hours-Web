# AI test bench report

Generated 2026-10-09T02:47:35.455Z. Data: starter. Maps: ironwood_crossing. Seeds per matchup: 2 (from 1). Time limit 25 game minutes. Start positions rotate with the seed.

## Summary

```
== candidate: 2 matches: 2 victories, 0 timeouts, 0 draws, 0 errors
   expert:Ironwood vs hard:Ironwood: expert:Ironwood 1/2 wins (50%, 95% CI 9%-91%), hard:Ironwood 1/2 (50%, 95% CI 9%-91%), 0 timeouts, 0 draws
   Elo: expert:Ironwood 1000, hard:Ironwood 1000
   speed: 6709 logic frames/s on average (6007 at the slowest; real time is 30)
== determinism: 2/2 replayed matches identical
```

## Build: candidate

2 matches: 2 victories, 0 timeouts (no winner, never counted as a win), 0 draws, 0 errors.

### Head to head

| matchup | first wins | 95% CI | second wins | timeouts | draws | median length |
|---|---|---|---|---|---|---|
| expert:Ironwood vs hard:Ironwood | 1/2 (50%) | 9%-91% | 1/2 (50%) | 0 | 0 | 10.8 min |

### Win-rate matrix

Row configuration against column configuration: wins / games (timeouts and draws are games without a win).

| | expert:Ironwood | hard:Ironwood |
|---|---|---|
| **expert:Ironwood** |  | 1/2 |
| **hard:Ironwood** | 1/2 |  |

### Elo

Maximum likelihood over all games, 1000 = a phantom opponent every configuration drew once against. Timeouts and draws count as half a win. Few games give wide error bars: read it together with the intervals above.

| configuration | Elo | games |
|---|---|---|
| expert:Ironwood | 1000 | 2 |
| hard:Ironwood | 1000 | 2 |

### Start position bias

Victories by start position over decided matches; a big difference points at the map, not at the AI.

| start | wins / games |
|---|---|
| 1 | 2/2 |
| 2 | 0/2 |

### Averages per configuration

| configuration | games | money gathered | money spent | peak army value | final army value | idle production | units built | units lost | objects destroyed |
|---|---|---|---|---|---|---|---|---|---|
| expert:Ironwood | 2 | 5,060 | 9,810 | 4,175 | 1,325 | 84% | 34 | 29.5 | 34.5 |
| hard:Ironwood | 2 | 6,190 | 10,925 | 3,550 | 1,425 | 83% | 42 | 32.5 | 31.5 |

### Speed

2 matches: 6,709 logic frames per second on average (median 6,709, slowest 6,007; the game runs at 30), mean 19,505 frames per match, map load and match setup 2.1 s, 7.6 s per match including the browser.

## Determinism

2/2 replayed matches reproduced the same CRC at every sample (and the same statistics).

| match | frames | CRC samples | result | divergence |
|---|---|---|---|---|
| candidate-ironwood_crossing-m1-s1 | 14,717 | 50 | identical |  |
| candidate-ironwood_crossing-m1-s2 | 24,293 | 81 | identical |  |

