# Working rules for this repository

Zero Hour in the browser (WebAssembly), built on TheSuperHackers' GeneralsGameCode. Chrome only, Zero Hour only.

## Testing

- **Never judge gameplay on the starter pack.** AI behaviour, player assists and balance are measured only on the real
  Zero Hour data. The starter pack is a placeholder game: one faction, a two-player map, no real units, powers or maps.
- The starter pack may still be used for checks that are not gameplay: the build, start-up, menus, the launcher, saving
  and loading, the automated smoke tests (`e2e.mjs`, `shell_flows.sh`), and quick development runs while writing code.
  Results from those runs are never reported as evidence that a gameplay change works.
- The real game data is never downloaded, bundled or copied into this repository or a cloud session. Gameplay tests run
  on the player's own computer with `scripts/gameplay/realdata_tests.sh` (`ZH_PATH`, optional `GENERALS_PATH`).
  The player commits the report folder under `test-reports/`; read `summary.md` and the `*.trace.txt` files from there.
- Every gameplay change comes with its real-data check: add it to `scripts/gameplay/realdata_tests.sh` (map, players
  with teams `difficulty:side[:variant][@team]`, trace words) and say what a pass looks like.

## Other standing rules

- One commit per feature or fix.
- Do not trigger GitHub Actions.
- Keep the original game's controls (for example, Escape opens the menu).
- Multiplayer work is paused.
