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
| Idle hotkeys | selection | Alt+I selects the next idle army unit and centres the view on it, Ctrl+Alt+I selects all idle army units, Alt+W the next idle worker (builders and supply gatherers). The "Idle" counter at the top right shows how many stand idle; its buttons do the same |
| Repeat production | orders | Select a production building: "Production" panel above the command bar, or Alt+Q. When its queue runs empty because the last unit came out, it queues that unit again, as long as the money stays at or above the reserve set in the options ("Repeat production keeps this much money", default 0) and the unit can still be built. Cancelling the queue by hand stops it until you queue a unit again. A "REPEAT" tag marks the buildings that repeat ("REPEAT (waiting)" while the money or the rules hold it back) |
| Info strip | display | A slim line at the top of the screen: income per minute (the game's own cash per minute: supplies, derricks, bounties), army value (build cost of the armed units, in transports too), game time, and actions per minute (your commands in the last minute of game time, not the camera). The toolbar's "Info" button hides it. Only your own figures |
| Camera zoom-out | view | Options → Player assists, "Camera zoom-out: N% of the game's limit" (100–300%, steps of 25): the mouse wheel zooms out that much further. The default view and the scripted cameras keep the game's height; more terrain is drawn while the camera is above it |
| Panel size | view | "Size of the assist panels and the info strip" (50–300%, steps of 10): the boxes and the text of the assist panels, the info strip and the overlays' labels, on top of the size that follows the resolution. The Player assists dialog itself keeps its size |

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
| Retreat | Scripted: `stance-retreat-heal`, `-repair`, `-rally` in `scripts/assistbench/scenarios/realdata` (see below). By hand: select units in a fight and press Alt+R three times. | `percent=70`; then `ASSIST stance retreat: unit N at P% goes to repair facility M` (vehicles, with a repair building), `... heal facility M` (infantry), or `... the rally point x,y` and `... parks at the rally point`; after the repair or heal, `... goes back to x,y at 100%`. A move order while it retreats: `... takes the order of the player (no retreat for 15 s)`. |
| Spread | Face an enemy with artillery or blast weapons and press Alt+S. | `ASSIST stance spread: ... moves apart` or `... steps aside`. |
| Split fire | Have 8+ units attack a few enemies and press Alt+X. | `ASSIST stance split: unit N leaves A (enough on its way) for B`. |

**Retreat when damaged, scripted** (`realdata_tests.sh assists`). The repair and heal buildings are not named: the
scenarios create the first structure of the America side with KindOf `REPAIR_PAD` / `HEAL_PAD` (else of any side) and
the trace names it on the line `ASSISTTEST created 1 x <template> for player 0`. Check that it is a building the game
really repairs vehicles / heals infantry at. A pass:
- `stance-retreat-heal`: four `... at 40% goes to heal facility M`, four `... goes back to x,y at 100%`, no `rally point`
  or `cannot` line, and the Rangers end where they stood. `ASSISTTEST cannot create @HEAL_PAD`: the data has no such
  structure; `cannot use facility` or `goes to the rally point`: the building does not take Rangers in.
- `stance-retreat-repair`: the same with three `... at 35% goes to repair facility M` and three `... at 100%`.
- `stance-retreat-rally`: six `... at 35% goes to the rally point`, six `... parks at the rally point`, three
  `... takes the order of the player`, no `facility` or `goes back` line.

**Idle hotkeys** (no rule of the match needed: they only select)
1. Scripted: `scripts/assistbench/scenarios/realdata/idle-select.json` (part of `realdata_tests.sh assists`). A pass: every
   check passes and the trace has `ASSIST idle army: 3 idle, selects unit N ChinaTankBattleMaster` (four times, a different
   N the first three), `ASSIST idle army: selects all 3` and `ASSIST idle workers: 1 idle, selects unit N ChinaVehicleDozer`.
2. By hand: build a few tanks, a dozer and a supply truck; leave some standing. Expect `ASSIST idle count army=A workers=W`
   whenever the numbers change, with A the idle army units and W the idle dozers and trucks (a truck on its supply round
   and a dozer at work are not counted). Alt+I: the view jumps to one idle unit after the other; Ctrl+Alt+I selects them
   all; Alt+W selects a worker that stands idle. A unit that is moving, fighting or inside a building is never picked.

**Repeat production** ("Player assists allowed" ticked)
1. Scripted: `scripts/assistbench/scenarios/realdata/repeat-production.json`. A pass: every check passes and the trace has
   `ASSIST repeat production: factory N on (nothing yet), reserve 0`, at least three
   `... queues AmericaInfantryRanger again (money M, cost C, reserve 0)`, `... reserve 1000 for player P`,
   `... waits: money M, cost C, reserve 1000` (M - C below 1000), `... queues AmericaInfantryRanger again (money 5000, ...)`
   and `... off`.
2. By hand: select a War Factory, queue one tank and press Alt+Q: `ASSIST repeat production: factory N on (<tank>), reserve R`.
   Each time the tank comes out: `... built <tank>` and `... queues <tank> again (money M, cost C, reserve R)` with
   M - C at least R. Set the reserve above your money in the options: `... waits: money M, cost C, reserve R` and the
   building shows "REPEAT (waiting)". Cancel the queue: `... queue emptied by the player, waits for a new order`. Lose the
   prerequisite (sell the radar for a unit that needs it): `... cannot build <unit> now (reason 1), waits`.
3. Save the game with repeat on and load it: `ASSIST loaded: allowed=1, repeat production on N buildings`, and it goes on.
4. Record the match and play it back: the same `queues ... again` lines and no `CRC Mismatch`.

**Info strip** (display only, works in every match)
1. Switch it on, start a skirmish, build a few units and harvest. Every ten seconds of game time the log has
   `ASSIST info income=I/min army=A time=m:ss apm=P`.
2. A pass: I matches the money that came in over the last minute (it is 0 before the first delivery and rises with each
   truck), A is the sum of the build costs of your armed units (it drops when one dies, a dozer or truck adds nothing),
   time is the game clock and P rises when you click and order fast and falls to 0 after a minute of doing nothing. The
   strip on screen shows the same figures and never anything about the enemy.

**Camera zoom-out and panel size** (no rule of the match needed: they only change the view)
1. Set the zoom-out to 200% in the options: `ASSIST camera zoom-out allowance 200% of the game's limit`.
2. In a match, roll the mouse wheel to zoom out as far as it goes: `ASSIST camera zoomed out to N% of the game's limit
   (allowed 200%)` lines up to N close to 200. The terrain reaches the edges of the screen (no black border inside the
   map). Backspace (camera reset) brings the usual height back. Set it to 100%: the camera comes down to the usual limit.
3. Set the panel size to 150%: `ASSIST panels scale 150% (S with the resolution)`. The formation and stance panels, the
   idle counter and the info strip are half again as large, text included; the dialog stays as it was.

**Replays:** record the match, play it back, and check that the log has no `CRC Mismatch` line.

### Known gaps

- **Retreat when damaged** finishes on the starter content (smoke checks only: infantry healed in an infirmary and back,
  vehicles repaired at a repair bay and back, the rally point and parking without one, the player's order winning, a full
  infirmary, units with kite on hurt in a fight). It has not been run on the real data: which Zero Hour buildings repair and heal,
  and whether real units get in, is what `stance-retreat-heal` / `-repair` / `-rally` check there.
- **Air coverage** was checked only with a stand-in anti-air tower.
- **Shared AI code:** stances use the same tactics code as the Expert AI (`AITacticsCore`). The Expert's behaviour was unchanged on the starter data (identical results and CRC timelines). The real-data AI runs in `scripts/gameplay/realdata_tests.sh` are the check for real factions.
