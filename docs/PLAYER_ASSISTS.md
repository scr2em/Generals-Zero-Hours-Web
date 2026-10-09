# Player assists

Optional helpers for the human player. They are all off by default: switch them on in **Options → Player assists**.
The assists that give orders work only in a match that allows them (**Player assists allowed** in the skirmish
setup). The display-only ones work everywhere. Orders from assists are game messages, so they are recorded in replays
and are the same on every machine.

| Assist | Kind | How to use it |
|---|---|---|
| Formations | orders | Select 2+ units: Formation panel on the left; Ctrl+Alt+0..6 (none, line, column, wedge, box, loose, keep shape), Alt+F next; right-drag on the ground aims the front line |
| Protect | orders | Select protectors, Alt+P, then click the unit or building (or press the hotkey group) to protect; Alt+U stops |
| Defence coverage view | display | Alt+D or the toolbar "Coverage" button: range rings of your defences, uncovered stretches of the base edge (ground and air) |
| Base under attack | orders | When the radar alert fires, a button appears (or Alt+A): idle army units nearby attack-move there; Alt+A again sends them back |
| Odds meter | display | With own armed units selected, Alt+O and hover a visible enemy: favourable / even / unfavourable |
| Unit stances | orders | Stance panel above the command bar for the selected units: Alt+K kite, Alt+R retreat when damaged (off / 30 / 50 / 70%), Alt+S spread out, Alt+X split fire. A player order always wins over a stance |

## Checks on the real game data

Gameplay is judged only on the real Zero Hour data (see `CLAUDE.md`). Until the scripted assist tests cover them,
these are manual checks: start the game with `-assistDebug` (web: add `?arg=-assistDebug` to the page address), play a
skirmish against an Easy AI with the assist switched on, and look for the `ASSIST` lines in the page's log.

**Defence coverage view**
1. Build one ground defence and one anti-air defence next to the base, then press Alt+D.
2. Expect `ASSIST coverage ground defences>=1 air defences>=1 bases>=1 uncovered ground steps=N air steps=M`.
3. N and M should fall as more defences ring the base.
4. On screen: rings around the defences and marks on the edge that is not covered.

**Base under attack**
1. Park 5–10 idle units near the base and let the AI attack (or hurt a building).
2. Expect `ASSIST alert raised at x,y`.
3. Press Alt+A: `ASSIST base defend: N units sent to x,y`, with N ≥ 1 and no busy units counted.
4. Press Alt+A again: `N units sent back`.

**Odds meter**
1. Select 8 tanks, press Alt+O, hover a single enemy infantry unit: expect `ASSIST odds kind=1 ...`.
2. Hover an enemy group clearly stronger than yours: expect `kind=-1`.

**Unit stances** ("Player assists allowed" ticked)

| Stance | Setup | Expected lines |
|---|---|---|
| Kite | Select 6 out-ranging units (artillery or rocket units) facing an enemy infantry group, press Alt+K, attack. | `ASSIST stance set mask=1 ...`, then `ASSIST stance kite: unit N steps back from M`. |
| Retreat | Select units in a fight and press Alt+R three times. | `percent=70`; then `ASSIST stance retreat: unit N at P% goes to repair facility M` (vehicles, with a repair pad), `... heal facility M` (infantry), or `... the rally point x,y`; after the heal, `... goes back to x,y`. |
| Spread | Face an enemy with artillery or blast weapons and press Alt+S. | `ASSIST stance spread: ... moves apart` or `... steps aside`. |
| Split fire | Have 8+ units attack a few enemies and press Alt+X. | `ASSIST stance split: unit N leaves A (enough on its way) for B`. |

**Replays:** record the match, play it back, and check that the log has no `CRC Mismatch` line.

### Known gaps

- **Retreat to a heal or repair building** has not been seen to finish: the unit reaches the building, but healing and the "goes back" line were not reached in a test.
- **Retreat to the rally point** has not been exercised.
- **Air coverage** was checked only with a stand-in anti-air tower.
- **Shared AI code:** stances use the same tactics code as the Expert AI (`AITacticsCore`). The Expert's behaviour was unchanged on the starter data (identical results and CRC timelines). The real-data AI runs in `scripts/gameplay/realdata_tests.sh` are the check for real factions.
