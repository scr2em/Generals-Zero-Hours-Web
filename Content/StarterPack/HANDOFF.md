# Handoff: the free starter content

For an AI coding agent or developer who has never seen the session that produced this. Read this first, then
`README.md` (how the pack is built) and `REQUIREMENTS.md` (what the engine needs from game data).

## Status and changelog (newest first; keep this section current)

* **Session 2: the starter game is complete and plays to the score screen (this entry replaces "known gaps" where they conflict).**
  A whole skirmish against the computer runs to **defeat** and to **victory** and ends on the score screen, with the computer
  building a base (power plant, barracks, factory by script) and attacking; no `ASSERTION FAILURE`, no `MISSING:` string and no
  audio error in the log of a match (`GeneralsMD/Code/Main/web/test/starter_match.mjs`, section 13). What changed:
  * *AI.* `TeamResourcesToStart = 1.0` (100 meant that no team was ever affordable: the AI built a power plant and two workers
    and stopped) and two build-order scripts in `gen/ai_scripts.py` (`IronwoodBuildBarracks` at once, `IronwoodBuildFactory`
    after 40 s) using `SKIRMISH_BUILD_BUILDING` (action 244, added to `tools/spk/scripts.py`); `GUI:ExpertAI` string for the
    difficulty the AI work adds. The same hunks are on the AI branch.
  * *End of game.* New `gen/mp_scripts.py` writes `Data/Scripts/MultiplayerScripts.scb` (victory, defeat and "you are out"
    scripts); without it the engine tracked who was alive but never showed a banner or left the game. Banner layouts in
    `gen/wnd_extra.py`. `tools/spk/scripts.py` got the three multiplayer conditions and `LOCALDEFEAT`.
  * *Screens.* `gen/wnd_extra.py` (new): options (with the custom detail pop-up), keyboard shortcuts, credits, save / load
    (full screen and pop-up), replays and the replay-save pop-up, in-game chat, player list, the helper pop-ups, end banners.
    `gen/keys.py` (new): names and descriptions of every key binding. The main menu has Skirmish, Play with friends, Load game,
    Replays, Options, Credits, Exit; the pause menu has Return, Options, Save / Load, Restart, Exit. `CommandMap.ini`:
    `OPTIONS` is bound to Escape (as in the original game; it opens the pause menu in a match) and the chat, player list,
    beacon and fast-forward keys exist.
  * *Fixes found by playing.* Combo-box lists were covered by the widgets below them (the writer now puts combo boxes last);
    check boxes drew their box over the label; the faction combo shows `SIDE:Ironwood`; tooltip titles showed a literal `&`
    (`HOTKEY_TEXT`); the production queue strip covered the command buttons, so only one unit could be queued; the starting cash
    option was ignored (`StartMoney` of the player template must be 0); every rally point was refused ("Units cannot reach that
    place": the engine asks for a locomotor named `BasicHumanLocomotor`); the score screen's column headings were hidden under
    its backdrop and "Winner" was cut off (the window holds the faction emblem); `GameLOD.ini` had `MaxTankTrackEdges = 0` (vertex buffer
    with no vertices, assertion at start up); `Animation2D.ini` was empty (a null animation template, thousands of assertions
    as soon as a building was damaged); audio event `NoCanDoSound` was missing; wins / losses on the setup screen stayed blank
    (hidden `ListboxInfo`); strings `Version:*` (replay list "Version" column), `NUMBER:<n>` (control-group message),
    `DOZER:*`, `RADAR:*`, `UPGRADE:*`, `SIDE:*`, `Chat:*`; objects got the unit voices and the construction loop; the HQ grants
    itself the radar upgrade (minimap); workers carry a mine-sweeper weapon so that an idle dozer does not assert; the HQ income
    is 50 every 5 s; the combat units say `AutoAcquireEnemiesWhenIdle = Yes Attack_Buildings` (without `Attack_Buildings` an
    idle unit or one on attack move never shoots at a structure, so an army that arrived at the enemy base stood there).
  * *One engine change* (`MainMenu.cpp`, `DoResolutionDialog`): the "keep this resolution?" box was created at a fixed corner
    (`CORNER` = 10, 10) and opened at the top left of the screen; it now passes -1, -1 and keeps the position of its layout
    (the middle of the screen). Nothing else in the engine was changed.
  * *Not done / known.* No second map (the pack has one, "Ironwood Crossing"); the art is still the generated placeholder art;
    a browser window smaller than 1024 x 768 with the launcher's "As set in the game's Options" makes the renderer print
    `No valid texture format found` at start up (platform code, release builds do not assert). A software rendered headless
    browser runs a match at 3 to 15 logic frames per second (30 are real time): a match test takes about half an hour.
* **Network (LAN) menus, for playing with friends over the virtual LAN of the web port's rooms**
  (`GeneralsMD/Code/Main/webnet/README.md`). New `gen/wnd_lan.py`: `LanLobbyMenu.wnd`, `LanGameOptionsMenu.wnd`,
  `LanMapSelectMenu.wnd`, `GameInfoWindow.wnd` (the box next to the lobby's game list) and `DisconnectScreen.wnd` (the
  code dereferences its buttons without a check, so a network game that lost a player would have crashed without it),
  plus the `GUI:`/`LAN:`/`Network:` strings the engine's LAN code fetches. The main menu got `ButtonNetwork`
  ("Play with friends", `MainMenu.cpp` pushes the lobby for it). Verified with two browsers: lobby, chat, host/join, start,
  lockstep with equal checksums (`webnet/test/lan_flow.mjs`). Not provided: `NetworkDirectConnect.wnd` (the lobby's
  hidden `ButtonDirectConnect` stands in; the room is the network).
* **Session 1, later: the pack runs in the engine.** With the runtime agent's display fixes the engine now shows the
  pack's main menu, the skirmish setup screen finds "Ironwood Crossing", Start loads the map, both sides get their
  base, the AI starts building (`Forcing build of power plant`), the control bar draws and takes a worker. Driven with
  `GeneralsMD/Code/Main/web/test/starter_flow.mjs` (new; clicks through the real menus in headless Chromium and prints
  the engine log after each step). Fixed on the way: `MainMenu.wnd` now uses the real `MainMenuInit` (a `[None]` init
  left the movie-break flag set and nothing rendered), `Maps/MapCache.ini` is generated (release builds read the standard map
  list from it, they do not scan `Maps\`), `DefaultStartingCash` in `GameData.ini`, `Multiplayer.ini` lives in
  `Default/` (the main file is a stub), `WeaponSet` blocks on every object and `PhysicsBehavior` on every mover,
  default teams in `SkirmishScripts.scb` (`team<SkirmishSide>`), `EVERYONE` on the audio types, and the strings
  `GUI:StartingMoneyFormat` / `MAP:StarterCrossing`.
* **Launcher, retail layouts.** Verified against the user's real folder listings (Zero Hour: `*ZH.big` + `Music.big` +
  `generals.exe`, no base BIGs; base game: `INI.big`... + `maps.big` + `Music.big`). e2e (94 checks) has fixtures that mirror
  both: ZH folder only, parent folder with both installs, and ZH then the optional add. `/generals` empty or missing is fine:
  the engine mounts it empty (`WebStorage.cpp` creates it) and the starter pack already runs that way.
* **Gameplay check (debug build, `starter_flow.mjs`)**: skirmish starts, bases and workers render with team colours and shadows,
  the HQ trains workers (money drops, queue shows), a single selected worker shows the three construct buttons, placing the
  power plant works (`Under construction: 39%`). Box-selecting two units shows only the common buttons (engine behaviour).
  `starter_flow.mjs` steps `r:X1,Y1,X2,Y2` (drag box) and `R:X,Y` (right click) were added.
  Added audio events for every fixed name the engine looks up (`PlaceBuilding`, `RallyPointSet`, `Beacon*`, `GUI*` fades ...).
  Still asserting in a debug build (harmless in release): `VehicleCrashesInto*Weapon` missing, `Unexpected player template`
  (LoadScreen.cpp hard codes the three original faction names), "unable to attack at all" filter, `UnderConstruction` audio
  name, WebD3D8 (now dxWebGL2) `VertexCount` once at start.
* **Browser scope: Chrome (Chromium) only** (user decision). Firefox and Safari are out of scope; the launcher may use
  `showDirectoryPicker`, OPFS `createSyncAccessHandle` and Keyboard Lock. Do not add fallbacks for other browsers; existing
  ones (the `<input webkitdirectory>` picker is what the tests drive) stay only because they cost nothing.
* **Fixed: units and buildings were invisible, and their shadows were black squares.** Root cause was an engine bug, not the
  W3D data: `ThingTemplate::m_assetScale` was never initialised in the constructor, so every object without an explicit
  `Scale = ` line got scale 0 (zero memory on wasm); `W3DModelDraw` then set `ObjectScale` 0 and the bounding sphere radius
  (`ObjectScale * radius`) became 0, so the scene culled the object. Trees only worked because their INI had `Scale = 1.0`.
  Engine fix (report to the coordinator): one line in `Core/GameEngine/Source/Common/Thing/ThingTemplate.cpp` (constructor sets
  `m_assetScale = 1.0f`). Data fix so the pack also works on unpatched engines: every object in `Object.ini` now has
  `Scale = 1.0`. The black squares were `sp_shadow` itself: `SHADOW_DECAL` is drawn with the multiplicative shader, so the
  texture must be white at the edge and darker in the middle (no alpha); `gen/textures.py::shadow_texture` fixed.
  `Buildable = Yes` is now explicit on the Ironwood objects (the field is also never initialised by the constructor).
* **Skirmish setup text** (found by the font agent): added the strings `GUI:Random`, `GUI:None`, `GUI:Observer` and `GUI:???`
  (the colour combo's "random" entry; the `.str` label check now allows `?`), and the column headings were hidden under
  the rows backdrop (later siblings draw on top in a `.wnd`), so the backdrop is now the first child.
* **Launcher: one folder pick for Zero Hour.** The page asks for the Zero Hour folder or any folder above it (Steam / EA app /
  Ultimate Collection library); `importer.detectInstall` finds Zero Hour (INIZH.big ...) and the base game it depends on
  (INI.big ...) inside the pick and imports both. Only when the base game is not found does a clearly secondary prompt appear. The
  starter content needs no `/generals` data at all (the engine runs with the folder absent). e2e: 77 checks, both detection cases.
* **End of the first half of session 1.** Everything below "What exists" is in the tree. The pack builds
  deterministically (about 20 s), the 28 pack tests pass (about 100 s), the launcher e2e passes.

## 1. Goal and the copyright rules (non negotiable)

Goal: an **original** free "starter content" game data set so the WebAssembly port of Command & Conquer Generals: Zero Hour
can boot to a main menu and play a small skirmish (one made-up faction against the computer, one two player map) without
anybody's copy of the original game, plus a launcher option that downloads it into the browser.

Rules:

1. **Nothing may be copied from Electronic Arts' game data**, not a texture, model, sound, string, INI block, window layout,
   map, font, icon, name or number table. Do not open, convert, trace, "redraw" or "improve" original assets and do not
   paste from them, even from memory of what a file contained. Do not put original data in test fixtures either.
2. **The only allowed source for formats and names is this repository's GPL source code** (`Core/`, `GeneralsMD/`,
   `Dependencies/`). File layouts, field names, window names the code looks up, event names and chunk ids are facts of the
   code; take them from there and cite the file in a comment (the existing modules do this).
3. Everything in the pack is **GPL-3.0-or-later** and generated from source: scripts in `gen/`, hand written text under
   `data/`. The only third party material is DejaVu Sans (Bitstream Vera licence, redistribution allowed) in
   `third_party/dejavu/`; see `NOTICE.md`. A new third party asset needs a licence that allows redistribution with a GPL
   project, an entry in `NOTICE.md`, and its licence text in the pack.
4. **Do not imitate the original game's look or names.** The faction is the "Ironwood Compact" (made up); the UI is its own
   design. No Command & Conquer, USA/China/GLA, Generals, Zero Hour trademarks or unit/building names in the content (the
   file `generalszh.exe` and engine identifiers like `ControlBar.wnd` are required by the engine and are not content).
5. **Generated binaries stay out of git** (`Content/StarterPack/.gitignore`). Commit sources and generators, never a built
   pack, `.big`, `.wav`, `.tga` or `.w3d`. The only committed binaries are the two DejaVu font files.
6. Authors of tools you add must keep them dependency free (standard library only) so the pack builds anywhere with
   Python 3; Pillow is allowed only for developer conveniences (`tools/preview_w3d.py`).

## 2. What exists

* One faction, the Ironwood Compact: HQ (builds workers), power plant, barracks, factory; worker, rifleman, rocketeer,
  scout buggy, tank; scenery (tree, pine, rock, flag). Economy: the HQ deposits income (`AutoDepositUpdate`), no supply
  gathering. Victory: a player is defeated when no `MP_COUNT_FOR_VICTORY` structure is left (engine rule).
* One two player map "Ironwood Crossing" (`gen/maps.py`, 160 x 160 cells, 1280 x 1280 playable units, start waypoints
  `Player_1_Start` (280, 280) and `Player_2_Start` (1000, 1000)), preview picture and `map.str`.
* Computer opponent: `SkirmishBuildList` in `data/Data/INI/AIData.ini` (what it builds and where) and
  `Data/Scripts/SkirmishScripts.scb` from `gen/ai_scripts.py` (three team types with production conditions; a finished
  team runs `IronwoodAttackWave` = `TEAM_HUNT`; two build-order scripts raise the barracks and the factory). The human side
  gets a script that starts battle music. `Data/Scripts/MultiplayerScripts.scb` (`gen/mp_scripts.py`) ends the match.
* Menus: main menu, skirmish options, skirmish map select, load screens, score screen, message box, quit menus, the
  control bar and its helper layouts (`gen/wnd_menus.py`, `gen/wnd_game.py`, DSL in `gen/wnd_widgets.py`), options, keyboard,
  credits, save / load, replays, chat, player list and the end banners (`gen/wnd_extra.py`), the network lobby
  (`gen/wnd_lan.py`), the interface art atlas and backdrops (`gen/ui_art.py`), about 400 strings (`gen/strings_data.py`,
  `gen/keys.py`).
* Audio: 24 effects and 4 music tracks synthesised in `gen/audio.py`, plus the audio INI files generated from the same
  tables. Effects are 16 bit PCM; music is IMA ADPCM (the encoder is in `tools/spk/wavfile.py`; the engine decoder
  reads it: `tests/test_engine_decoder.py` compiles `WebAudioDecoder.cpp` and compares).
* Models (13 placeholders plus the engine's fixed name models), terrain tile sheets, particles, shadow, sky, water
  (`gen/models.py`, `gen/textures.py`). **These are placeholder art; see "Art pipeline" below.**
* Launcher (under `GeneralsMD/Code/Main/web/`): two options, described in section 4.
* Tools and tests under `tools/` (section 3).

## 3. Repo map of the area

```
Content/CMakeLists.txt              web-only target `starter_pack` (see section 6)
Content/StarterPack/
  build_pack.py                     python3 build_pack.py <out> [--big] [--name NAME] [--no-generate]
  README.md REQUIREMENTS.md NOTICE.md HANDOFF.md .gitignore
  data/                             hand written files, laid out like an install folder. Data/INI/*.ini etc.
                                    (.ini .str .wnd .txt are converted to CRLF by the build; see "conventions")
  gen/                              generators; each module has generate(emit); emit(path, bytes|str)
    textures.py ui_art.py models.py audio.py maps.py ai_scripts.py
    wnd_widgets.py wnd_menus.py wnd_game.py wnd_extra.py wnd_lan.py strings_data.py keys.py textdb.py zz_strings.py misc.py
    mp_scripts.py (victory / defeat scripts)
  third_party/dejavu/               font files + licence
  tools/spk/                        format writers (and readers used by the tests)
    bigfile.py tga.py image.py (Canvas) wavfile.py (PCM + IMA ADPCM) synth.py strfile.py (STR/CSF)
    w3d.py wnd.py datachunk.py mapfile.py scripts.py engine_schema.py util.py
  tools/validate_pack.py            follow every reference in a built pack
  tools/engine_requirements.py      INI folders the engine loads (from its sources) vs the pack
  tools/check_wnd_names.py          window names looked up by code vs layouts
  tools/preview_w3d.py              contact sheet PNG of models (needs Pillow)
  tools/tests/                      unittest suite (+ engine_decode_check.cpp)
GeneralsMD/Code/Main/web/           launcher: shell.html (page), importer.js (import + starter download), serve.py,
                                    coi-serviceworker.js, test/e2e.mjs (Playwright)
GeneralsMD/Code/Main/webtest/       the runtime agent's boot harness: smoke.mjs (use it as the example harness)
```

How the build works: `collect()` copies `data/` (CRLF conversion for text), then runs every `gen/*.py` module in name order
and collects what they `emit`. Paths are lowercased; a path emitted twice is an error. `write_tree` writes the files,
`write_manifest` writes `manifest.json` (`path`, `size`, `sha256` per file, `totalSize`, `version`, `license`).

## 4. The launcher (two options)

`shell.html` is the page (it is baked into `z_generals.html` at link time: after editing it, relink the target). It shows
two cards. `importer.js` does the work.

1. **"Use my Zero Hour installation"**: one main action, "Select folder...". The user picks the Zero Hour folder or a folder
   above it (a library holding both games); `detectInstall` finds Zero Hour by its `*ZH.big` archives and the base game by
   `INI.big`/`W3D.big`/`Textures.big`/`Terrain.big` (never by exe name or `Music.big`: both retail installs have a
   `generals.exe` and a `Music.big`). Zero Hour is imported; if the pick also holds the original game its archives are added
   silently. Play is enabled after the Zero Hour import alone. After a Zero Hour import a small optional link
   ("Add the original Generals files (optional, improves missing art)", `#pick-generals`) opens a second pick for the
   original game; it never blocks. No other base game UI exists.
   What is copied (`planImport`, lower-cased names, into OPFS `/game` and `/generals`, manifest `kind: "install"`):
   * Zero Hour: every `*.big` at the top level, `Data/**` and any other sub folder, plus ONE executable stored as
     `generalszh.exe` (the engine fingerprints it at start up, `GlobalData::generateExeCRC`; it only matters for network and
     replay compatibility). `generalszh.exe` is preferred, else `generals.exe` (the retail name; the real binary is `game.dat`).
     Skipped: all other top level files (`.dll`, `.sys`, `.dat`, `.bmp`, `WorldBuilder.exe`, the `00000000.016/.256`
     fingerprint files, ...; the engine reads none of them), `MSS/` (Miles codecs, unused by the web audio), `UserData/` (user
     data lives in OPFS `/userdata`), `Movies/`, `.bik`.
   * Original Generals (`/generals`): only top level `*.big`, because `StdBIGFileSystem::init` loads nothing else from the
     Generals install directory. Skipped: `maps.big` (original game maps, optional; `includeOptional` brings it back), any
     archive with the same name and size as one already copied from Zero Hour (`Music.big` is in both installs), everything else.
   * Sizes: depend on the install; Zero Hour was 936.6 MB / 126 files before this filter (now fewer: the loose top level files
     and `MSS/` are gone). The base game costs roughly its archive total minus `maps.big` minus the duplicate `Music.big`.
   * Quota errors name what is needed and what is available, explain that Chrome limits a site to a share of the free disk
     space and that freeing disk space and retrying works (`importer.quotaMessage`). The check covers both installs of a
     combined pick up front.
   * **Import modes.** `runImport(plan)` dispatches on `plan.mode` (default `'copy'`, `copyIntoOpfs`). A copy-less "read in
     place" mode (File System Access handles, FileReaderSync backed WasmFS backend) is added with
     `registerImportMode(name, fn)`; `planImport` would set `plan.mode`, `writeManifest(plan)` records the target so the
     status line keeps working. Not implemented here. The shell calls only `planImport`/`importPlan`/`runImport`.
   * Browser scope: Chrome/Chromium only (see the status block).
2. **"Play with free starter content"**: `downloadStarter` fetches `starterpack/manifest.json` (relative to the page),
   downloads every file listed (4 in parallel, sha256 checked, progress shown), clears `/game` and `/generals`, writes the
   files into OPFS `/game` and records a manifest with `kind: "starter"`. The UI says plainly that this is an original
   placeholder game (`#starter-note`, a badge in the game page via `#mode-badge`, the text `#play-mode`). The two
   modes replace each other (the page asks before replacing an own install).

The page loads the game module only after the user clicks Play (needed so the Web Audio context starts running);
`window.zhWebAudio.resume()` is called on later clicks/keys as a fallback.

The pack is expected at `<site dir>/starterpack/` (`STARTER.baseUrl` in `importer.js`). In the CMake build the target
`starter_pack` generates it there.

## 5. Setup on macOS

Tested on Linux only; the macOS commands below are the equivalents.

```sh
# tools
brew install python@3 cmake ninja node git            # Python 3.9+ (developed on 3.13), CMake 3.20+, Node 20+
python3 -m pip install pillow                          # optional: only tools/preview_w3d.py
# emscripten SDK (the repo's presets use $EMSDK)
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk && cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
source ~/emsdk/emsdk_env.sh                            # sets $EMSDK; do this in every shell (the session used emcc 6.0.11)
# Playwright with its own Chromium (do NOT use /opt/pw-browsers, that path exists only in the Linux sandbox)
mkdir -p ~/pw && cd ~/pw && npm init -y && npm i playwright && npx playwright install chromium
export NODE_PATH=~/pw/node_modules                     # the harness scripts `require('playwright')`
```

Build the engine (a debug flavour with symbols and runtime asserts is what you want while developing content, because
unknown INI fields then assert and show in the log):

```sh
cd <repo>
cmake --preset emscripten -B build/em-dbg -DRTS_WEB_DEBUG=ON -DRTS_DEBUG_LOGGING=ON   # configure (first time fetches dx8 headers etc.)
cmake --build build/em-dbg --target z_generals -j8                                  # about 80 MB wasm, several minutes
```

Generate the pack and serve everything (cross-origin isolation is required: `serve.py` sends COOP/COEP headers):

```sh
python3 Content/StarterPack/build_pack.py build/em-dbg/GeneralsMD --name starterpack    # -> build/em-dbg/GeneralsMD/starterpack
python3 GeneralsMD/Code/Main/web/serve.py --port 8000 --dir build/em-dbg/GeneralsMD
# open http://127.0.0.1:8000/z_generals.html  (Chrome or Chromium; a localhost URL counts as a secure context)
# click "Play with free starter content", then Play.
```

(With the CMake wiring from section 6 `cmake --build build/em-dbg --target starter_pack` does the middle step.)

Tests:

```sh
cd Content/StarterPack
python3 -m unittest discover -s tools/tests                  # ~100 s; STARTERPACK_SKIP_SLOW=1 skips the four-build pack tests
python3 build_pack.py /tmp/sp && python3 tools/validate_pack.py /tmp/sp
python3 tools/engine_requirements.py /tmp/sp
python3 tools/check_wnd_names.py /tmp/sp/StarterPack          # note: this one wants the pack directory itself
```

Launcher end to end (needs a build of the small `web_platform_test` target and a "fake install"; the test copies the pack):

```sh
cmake --build build/em-dbg --target web_platform_test
export CHROMIUM_PATH=...   # optional; without it Playwright's bundled Chromium (the macOS case) is used
node GeneralsMD/Code/Main/web/test/e2e.mjs --site build/em-dbg/GeneralsMD --fake <dir with ZeroHour/ and Generals/ fake folders> \
     --out /tmp/e2e --starter build/em-dbg/GeneralsMD/starterpack
```

`--fake` needs a folder `ZeroHour/` with `INIZH.big`, `Data/INI/GameData.ini` (26 bytes), `Maps/alpine/alpine.map`, an `.exe`
and a `.bik` and a `Generals/` folder with `INI.big` (1,000,000 bytes) and `Data/Readme.TXT`; the test file
`GeneralsMD/Code/Main/webtest/gen_synthetic_data.py` shows how such synthetic folders are made. The e2e creates its own
private copy, adds `generalszh.exe`, and expects 4 files in the Zero Hour import. Remove `<out>` between runs (a persistent
profile lives in it). The e2e of the session passed with 70 checks. `smoke.mjs` hard codes
`/opt/pw-browsers/chromium-1194/...` in its `exe` constant: change it to use `chromium.executablePath()` on macOS.

## 6. CMake wiring

`Content/CMakeLists.txt` defines the custom target `starter_pack` (ALL, option `RTS_BUILD_STARTER_PACK`, default ON): it runs
`build_pack.py <STARTER_PACK_SITE_DIR> --name starterpack`, where `STARTER_PACK_SITE_DIR` defaults to
`${CMAKE_BINARY_DIR}/GeneralsMD`, the folder with `z_generals.html`. **One line to wire it in, in `cmake/emscripten.cmake`
(only the web build compiles it):**

```cmake
add_subdirectory(Content)
```

This line was NOT yet added (outside the content agent's ownership); the file was verified in a scratch CMake project.

## 7. Booting the engine with the pack and reading the log

`GeneralsMD/Code/Main/webtest/smoke.mjs` imports a data folder through the launcher, presses Play and prints the console log
of the engine (debug logging prints through `console`, prefixed with a time). Pattern used all session:

```sh
# bootdata/ZeroHour = the built pack without manifest.json, plus an empty inizh.big; bootdata/Generals = ini.big (any BIG)
node GeneralsMD/Code/Main/webtest/smoke.mjs --site build/em-dbg/GeneralsMD --data <bootdata> --log boot.log --shot boot.png \
     --wait 40 [--arg -headless] [--arg -noshellmap]
```

(the importer wants a signature archive `inizh.big` in the folder, hence the tiny BIG; `tools/spk/bigfile.py` writes one.
The starter download path does not need it.) Useful greps over the log:

```sh
grep -n "ASSERTION FAILURE\|Missing asset\|Failed to create Render Object\|Targa: Failed\|not found" boot.log | grep -v "Error finding file\|Got so far"
```

Facts learned the hard way:

* `Error opening directory ...` and `Error finding file ... Got so far ...` are normal probing of optional paths.
* With `-headless` the engine initialises everything except the display and stops after `Shell:push(Menus/MainMenu.wnd)`;
  the dummy window manager prints `setControlCommand: Window is not a button` for the control bar (ignore).
* `-file <map>` and `-map` exist only in `RTS_DEBUG` builds; this build has debug logging but not `RTS_DEBUG`. To start a
  skirmish you have to click through the menus (drive it with Playwright mouse events, see `smoke.mjs --input`).
* Strings: LF only. INI: unknown fields assert with the field name in the log.
* Boot log at the end of session 1 (non headless): all INI, strings, textures, models loaded with no pack related
  assertion; then `Could not do WW3D::Begin_Render()!` and `ASSERTION FAILURE: VertexCount, dx8vertexbuffer.cpp, 84` /
  `Failed to find a valid color mode`, which are the display bring-up issues of the runtime agent, not content.

## 8. Requirements summary (details in `REQUIREMENTS.md`)

* Every `Data\INI\<X>` the engine loads must have at least one file or the engine throws at start up (72 paths;
  `tools/engine_requirements.py`). One comment line is enough where content is not needed.
* `Data\english\Language.ini`, `HeaderTemplate.ini`, `CommandMap.ini`; `Data\Generals.str` (LF endings!).
* Window layouts for every screen the flow opens; callbacks must be names compiled into the engine
  (`FunctionLexicon`); window names are looked up by string in code (`tools/check_wnd_names.py`).
* `PlayerTemplate` entries `FactionObserver`, `FactionCivilian` and the playable faction; `Object` templates with the right
  `KindOf`; `AIData` `SkirmishBuildList` starting with the command center.
* Map: chunks in the right order, `Player_N_Start` waypoints, skirmish sides, scripts (`SkirmishScripts.scb`).
* Audio: `AudioSettings`, `DefaultSoundEffect`/`DefaultMusicTrack`/`DefaultDialog`, event names the code looks up.
* Hard coded asset names: models `Locater01`, `Locater02`, `SCMNode`, `new_skybox`, `MoveHintName`; textures `shadow`,
  `TBBib`, `TSNoiseUrb`, ... (list in REQUIREMENTS.md section 5).
* `generalszh.exe` must exist (checksum only).

## 9. Known gaps and TODO, ranked

*Written before the engine ran the pack; most items below are done (the status block says which). What is still open: the art
(placeholder), animations (D13: rigid models), balance (D12: one match won and one lost in the tests, nothing tuned), a second
map, cursors (A3: the web layer maps them), the load screen's hard coded faction names (it takes them from `ChallengeMode.ini`
now) and eva / speech lines (not provided).*

**A. Blocks reaching the main menu (engine side, not content)**

1. Display bring-up (WebD3D8, now dxWebGL2; WW3D2): `Could not do WW3D::Begin_Render()`, vertex buffer assertion, no valid colour mode.
   Owner: the runtime agent / `Dependencies/WebD3D8` (now the dxWebGL2 library), `Core/GameEngineDevice`. Until this works nothing is visible.
2. **Text rendering**: windows draw text through GDI (`CreateFont`, `TextOut`...), which `Dependencies/WebCompat/src/win32_window.cpp`
   stubs (`CreateFontA` returns a dummy handle, `AddFontResourceA` returns 1). Without a real implementation (for example
   FreeType-in-wasm rasterising the bundled `Data/Fonts/DejaVuSans*.ttf`, or canvas 2D text uploaded as textures) every label
   is blank. Change in WebCompat / the W3D display string code; no content change can fix it. `Language.ini` already
   names `DejaVu Sans` everywhere and lists the files in `LocalFontFile`.
3. Cursors: `LoadCursorFromFile` is a stub that returns a stock handle; `Mouse.ini` entries point at files that do not exist.
   Harmless, but there is no cursor art; the web layer would need to map cursor kinds to CSS cursors.

**B. Main menu content (untested in a running engine)**

4. Verify `Menus/MainMenu.wnd` renders and responds: `MainMenuInput` shows the dropdown `MapBorder2` on first input; `Skirmish`
   and `Exit` buttons must work. Exit uses `QuitMessageBox` when not windowed.
5. Check `WindowTransitions.ini` groups against what menus call (`reverse()` on an unknown group dereferences null).
6. `MapCache`: the setup list is built from `Maps\*\*.map`; watch the log for `MapCache::addMap` and the name from `map.str`.

**C. Skirmish setup**

7. Click through `SkirmishGameOptionsMenu.wnd` and `SkirmishMapSelectMenu.wnd`; both are generated and only syntax checked.
   The code looks up 51 window names in the options layout; `check_wnd_names.py` reports only `ListboxInfo` as missing
   (optional) but behaviour is untested (slot combo boxes, the start button, difficulty).
8. Load screen (`MultiplayerLoadScreen.wnd`): `LoadScreen.cpp` expects specific window names (checked) and the images named
   in `PlayerTemplate` (`LoadScreenImage`).

**D. Gameplay**

9. Map loading: `HeightMapData`/`BlendTileData`/`SidesList` readers were written from the engine's parsers and round trip,
   but no engine has loaded the map yet. Expected trouble spots: terrain class names must equal `Terrain` blocks
   (`SwGrass` etc., done), tile indices in range (tested), world dictionary keys, lighting chunk length.
10. Skirmish players: `Player::initFromDict` needs a skirmish side whose template `Side` matches (`SkirmishIronwood`); the
    AI needs a dozer-capable factory (the HQ builds workers) and `SkirmishBuildList`. Watch for `Could not find skirmish
    player for side ...` and `no team or player named ...` asserts.
11. Control bar: command buttons need `IMAGE` status; the scheme positions override some layout positions; every command
    set the HQ, factories and units use must exist in `CommandSet.ini`. Production, building placement
    (`Locater01/02`), rally points, selling.
12. Balance: costs, build times, AI team sizes were set by reasoning, never played.
13. Unit art has no animations (rigid bodies); infantry slide. Add walk cycles via `Animation` in `w3d.py` or leave.
14. `MultiplayerScripts.scb` not provided (not needed: victory rules are in `VictoryConditions`).
15. Eva/speech lines, campaign, options screens, replay screens: not provided (optional, buttons are not offered).

## 10. Conventions

* **Deterministic output.** No timestamps, no `random` module (use `spk.util.Rng`), no dict ordering reliance across Python
  versions beyond insertion order, no floating point formatting surprises. `tests/test_pack.py` builds twice and compares.
* **Paths in the pack are lower case**, forward slashes, relative to the install root (`art/w3d/iw_hq.w3d`). The engine and the
  OPFS layer are case insensitive, the manifest and tests assume lower case. Names inside INI files keep their natural case.
* **Text files**: `.ini .str .wnd .txt` under `data/` are converted to CRLF at build time, **except** content emitted by
  generators (emit exactly what you want). `Generals.str` and `map.str` must be LF (the engine mis-parses CRLF); the string
  generator writes LF and no `data/` file may be named `*.str`.
* **Formats**: TGA types 2/10 (id length 0, top left origin, RLE runs per line), WAV PCM or IMA ADPCM mono 22050 Hz, W3D
  written by `tools/spk/w3d.py`, maps by `tools/spk/mapfile.py`, scripts by `tools/spk/scripts.py`, WND by `tools/spk/wnd.py`.
* **Names**: everything of the faction is prefixed (`Ironwood...`, `iw_...`, `sp_...` for pack textures, `Starter...` for
  events/particles). Model names must be at most 15 characters (W3D name field); mesh full names at most 31.
* One module per kind of output in `gen/`; shared text goes through `gen/textdb.py` so labels cannot diverge.
* A new engine fact goes into `REQUIREMENTS.md` with the source file that fixes it; a new format gets a reader in `tools/spk`
  and a round trip test.

## 11. Art pipeline for higher-quality assets

The placeholder art is procedural (`gen/models.py` builds boxes, prisms, spheres from `Mesh.add_*`; `gen/textures.py` draws noise).
Replace it with real assets **made originally by you or licensed for redistribution under the GPL's terms** (never original
game assets; no ripping, no tracing). Keep the file names and INI references stable (see 11.5).

### 11.1 What `tools/spk/w3d.py` supports, and what it does not

Supported (all tested by `parse_w3d` and `tests/test_content.py`):

* `Mesh`: arbitrary indexed triangle meshes via `add_vertex(position, normal, uv, color)` + `add_triangle(a, b, c)` (the `add_box`,
  `add_prism`, `add_sphere`, `add_wedge` helpers are built on those); optional per-vertex RGBA colours (`DCG` chunk); one
  material, one shader, **one texture, one texture stage, one pass**; user text chunk; mesh `attributes` flags (collision
  physical/projectile/vis/camera/vehicle, hidden, **two sided**, cast shadow).
* `Model`: a skeleton (`add_bone(name, parent, translation, rotation)`, a ROOT bone is always present), several meshes each
  attached to one bone (`add_mesh(mesh, bone)`), one HLOD with one LOD level listing the meshes. A mesh follows its bone
  rigidly.
* `Animation`: translation channels per axis and quaternion rotation channels per pivot, uncompressed, fixed frame rate
  (`W3D_CHUNK_ANIMATION`). Not used by the placeholder models yet. A walk cycle needs separate meshes per limb attached to
  bones plus an `Animation` named `<Model>_<Anim>`.
* `Shader` presets: opaque, alpha blend, alpha test, additive (`Shader.alpha_blend()` etc.; raw fields available).
* `VertexMaterial`: ambient/diffuse/specular/emissive, shininess, opacity, translucency.
* House colour: a mesh whose name starts with `HOUSECOLOR` is recoloured with the owner's colour by the engine's
  `Recolor_Mesh` (`W3DAssetManager`), provided `OkToChangeModelColor = Yes` in the `W3DModelDraw` block. Put the team colour
  parts in a separate, untextured `HOUSECOLOR` mesh as `models.py` does.

Not supported (add to `w3d.py` if needed; the layouts are in the engine's `w3d_file.h` and `MeshModelClass::Load_W3D`):

* **Skinning** (per vertex bone influences, `W3D_CHUNK_VERTEX_INFLUENCES`): meshes are rigid per bone. Characters animate as
  separate meshes on bones (as many classic RTS units do).
* **Multiple LODs** (HLOD with several levels and switch distances), proxy/aggregate objects (`W3D_CHUNK_AGGREGATE`), `BOX`
  collision boxes (the chunk id exists as a constant, no writer), null objects, dazzles, emitters, `HModel`.
* **Compressed animations** (time coded / adaptive delta) and bit channels (visibility); only raw channels are written.
* Multi texture stages, second UV set, detail/bump maps, multiple passes, per vertex material arrays (one material per mesh;
  split a mesh per material), shader presets beyond the four helpers (the raw struct is exposed), sort levels
  (the header writes sort level 0 = opaque; set it in `Mesh.to_chunk` if you need transparent ordering), texture animation
  info beyond the defaults, `material_info` damage stages.
* Normal maps are not a thing in this renderer (fixed function pipeline era).

### 11.2 Engine and renderer limits to respect

* **16 bit indices.** `IndexBufferClass` keeps `unsigned short index_count` (`Core/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.h`):
  stay far below 65 535 indices (21 845 triangles) per mesh; split bigger meshes. Vertex counts per mesh stay below 65 535.
* **Budgets.** The engine draws every unit with its own draw calls and, in the browser, through WebGL2 on top of an emulated
  D3D8 layer, so draw calls matter more than triangles. Suggested budgets (a 2003 RTS with hundreds of units): infantry
  100 to 400 triangles, vehicles 800 to 2500, buildings 2000 to 8000 (split into few meshes), scenery 100 to 800 for trees.
  Prefer **one mesh per material** and as few materials as possible per model (every mesh is a draw call and
  `HOUSECOLOR` adds one). A whole model of 3 to 6 meshes is comfortable; 30 is not.
* **Textures.** Power of two sizes (the converter that loads TGA/DDS does not require it, the old hardware path assumes it);
  typical 128 to 512 for units, 512 to 1024 for buildings. Formats: uncompressed TGA 24/32 bit (types 2 and 10) and DDS (DXT1/3/5)
  from `Art\Textures`; the engine builds mip maps itself for TGA. The UI atlas must stay at most 1024 square for the old
  texture-size assumptions of `MappedImages` (`TextureSize_<n>`).
* Texture names in a mesh must equal a file in `Art\Textures` (lower case matching); a missing texture draws the `missing.tga`
  placeholder.
* **Terrain** tile sheets: multiples of 64 pixels, up to 10 x 10 tiles per sheet (`WorldHeightMap::countTiles`); the reader
  ignores the TGA origin flag and id field (see REQUIREMENTS.md section 5).
* Total texture and model data is downloaded by the launcher: keep the whole pack in the low tens of MB.

### 11.3 Bone and sub-object names the game logic expects

Names are referenced from INI, not hard coded, mostly in the `W3DModelDraw` block (`Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp`,
field table near line 1421 `ModelConditionInfo`):

* Turrets: `Turret = <bone>`, `TurretPitch = <bone>`, `TurretArtAngle`, `TurretArtPitch`; the weapon's `FireFX`/projectile
  positions come from `WeaponFireFXBone = PRIMARY <prefix>`, `WeaponLaunchBone`, `WeaponMuzzleFlash`, `WeaponRecoilBone`,
  `WeaponHideShowBone`. The engine appends a two digit index to the prefix (`Muzzle01`, `Muzzle02` ...) and finds the bone by
  name in the model's hierarchy (`getPristineBonePos`). Put muzzle bones at the barrel tip, turret bones at the pivot.
* `ParticleSysBone = <bone> <ParticleSystem>`, `ExtraPublicBone` (bones the logic may query), `ShowSubObject`/`HideSubObject`
  (switch meshes by name: names are `<model>.<mesh>`).
* **Condition states** pick the model and animation: `DefaultConditionState`, `ConditionState = DAMAGED / REALLYDAMAGED /
  RUBBLE / MOVING / ATTACKING / ...` with their own `Model =`, `Animation = <model>.<anim>`, `AnimationMode`, and for
  structures the construction states and `SOLD`/rubble variants. The bit names are in `GeneralsMD/Code/GameEngine/Source/Common/BitFlags.cpp`
  (`ModelConditionFlags`, e.g. `PARTIALLY_CONSTRUCTED`, `ACTIVELY_BEING_CONSTRUCTED`). The damage and construction models usually
  share the skeleton of the pristine model (same bone names) so the INI can switch between them.
* Animations are separate `.w3d` files referenced as `Animation = <file>.<name>` (file name without extension, then the animation
  name inside it), skeleton compatible with the model's hierarchy name.
* Shadows: `Shadow = SHADOW_DECAL` (a texture, `ShadowTexture`, no mesh needed) or volume shadows (`SHADOW_VOLUME`: meshes with the
  cast-shadow flag; the starter pack uses decals only).
* `HOUSECOLOR*` mesh names, see 11.1. Names starting `ZHC` for textures are hue shifted for house colour.
* Selection and collision come from the `Geometry*` fields of the Object, not from the mesh.

Read `W3DModelDraw.cpp` (`ModelConditionInfo::validateStuff`, `findPristineBone`) before inventing names; run the engine in a
debug build, which asserts on a missing bone that INI asked for.

### 11.4 Recommended workflow

Nothing in `tools/` converts external formats today. Options:

1. **Author in Blender, export glTF 2.0 (or OBJ), convert with a small script on top of `spk/w3d.py`.** Sketch for
   `tools/gltf_to_w3d.py` (standard library only; glTF is JSON plus a binary buffer): read `nodes`/`meshes`; for each node
   with a mesh create `Mesh(name, texture=<base colour texture file name>)` and fill it with `add_vertex(pos, normal, uv)` and
   `add_triangle` (convert the coordinate system: glTF is Y up, right handed, the engine is Z up, X right, Y forward:
   `(x, y, z) -> (x, -z, y)` and flip the triangle winding if you mirror); turn the node tree into `Model.add_bone` calls and
   attach each mesh to its node's bone; name meshes `HOUSECOLOR*` for team colour parts; map glTF alpha modes to
   `Shader.alpha_blend()`/`alpha_test()`; split meshes over 20 000 triangles; write `art/w3d/<name>.w3d` and copy textures to
   `art/textures/` (convert PNG to TGA with `spk.image.Canvas` or Pillow). Add a round trip test (`parse_w3d`) and keep the
   converter dependency free. For animation, sample node transforms per frame into `Animation.set_translation/set_rotation`.
   No such converter exists yet; it is the best first tool to write.
2. **Existing community Blender W3D add-ons** (several GPL or MIT licensed add-ons exist for exporting W3D) can write richer
   files (skinning, compressed animation). Check the licence of any add-on before using it and before copying any of its code
   into this repo (GPL compatible, attribution in `NOTICE.md`); using an add-on as an external authoring tool does not make its
   output a derivative work, but verify the add-on's own terms. Whatever you use, verify the result with `spk.w3d.parse_w3d`
   (structure) and in the engine (behaviour).
3. Keep **source art** (`.blend`, layered images) outside the generated pack, for example in `Content/StarterPack/art_src/`
   (committed; textual or small formats preferred), and make `gen/models.py` load and convert them so the build stays
   deterministic (`build_pack.py` must not require Blender).

Textures can be painted in any tool and saved as PNG in `art_src/`; the build converts them with `spk.image`/`spk.tga` (RLE TGA).

### 11.5 Swapping a placeholder without breaking references

* The references are by **file name** (`Model = iw_hq` in `Object.ini`, `ShadowTexture`, `ButtonImage` = a mapped image name
  in `data/Data/INI/MappedImages/HandCreated/starterui.ini`). Keep the model name (`iw_hq`...) and replace the content that
  `gen/models.py` emits under `Art/W3D/<name>.w3d`. For sub-parts the INI names (turret/muzzle bones, `HideSubObject`), keep or
  update them in `Object.ini` in the same change.
* Keep the placeholder's footprint (the `Geometry*` fields of the Object) or update them: they drive selection, collision and
  pathfinding, not the mesh.
* Keep the HOUSECOLOR convention and scale (units: world units, roughly 1 unit = 1 foot; the placeholder tank is about 22 units long).
* Preview without the engine: `python3 tools/preview_w3d.py sheet.png art/w3d/iw_hq.w3d ... [--palette art/textures/sp_iw.tga]`
  (flat shaded, painter's algorithm, needs Pillow; it shows geometry and palette coloured textures only).
* Verify structurally: `python3 tools/validate_pack.py <pack>` (every model referenced exists and parses) and
  `python3 -m unittest discover -s tools/tests`. Verify in the engine: boot with the pack (section 7) and look for
  `Failed to create Render Object` / `Missing asset` in the log; in game select the object and check size, team colour, shadow
  and, for armed units, that shots leave from the muzzle bone.

## 12. Next steps

1. Art (the work that goes on): write `tools/gltf_to_w3d.py` (section 11.4), then replace models and textures one by one,
   keeping the file names, bone names and `Geometry*` footprints (11.5), and add unit animations. After every batch run the
   tests, `validate_pack.py` and a match (section 13): the AI branch and the platform tests use this pack as their fixture.
2. Balance with the computer opponent: Hard attacks at about 2 minutes of game time and wins against a passive player in
   about 5 minutes; costs, build times and team sizes are in `Object.ini`, `AIData.ini`, `gen/ai_scripts.py`.
3. A second map (`gen/maps.py` writes one; the writer takes any size) and a second faction if wanted.
4. Eva / speech, unit voices that differ per unit, a real menu video.

## 13. Rebuilding, validating and playing a match

```sh
source ~/emsdk/emsdk_env.sh
# configure once; the two debug options put the engine's assertions and its log into the console of the browser
cmake --preset emscripten -B build/em-p -DRTS_DEBUG_LOGGING=ON -DRTS_DEBUG_CRASHING=ON
ninja -C build/em-p z_generals starter_pack          # the pack goes to build/em-p/GeneralsMD/starterpack (about 20 s)
cd Content/StarterPack
python3 -m unittest discover -s tools/tests          # 31 tests, about 2 minutes
python3 build_pack.py /tmp/sp && python3 tools/validate_pack.py /tmp/sp      # 200 files, 0 errors, 0 warnings
```

Content only (no engine rebuild): change a generator or a file under `data/`, run `ninja -C build/em-p starter_pack`, reload
the page (the launcher's "Play with free starter content" downloads the new files).

Drive the real game (headless Chromium, software WebGL; `PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers NODE_PATH=...` on the Linux
sandbox, see section 5 for a Mac):

```sh
T=GeneralsMD/Code/Main/web/test
node $T/starter_flow.mjs --site build/em-p/GeneralsMD --steps "w:3 m:300,300 w:1 c:185,221 w:5 s:skirmish"   # menus, screenshots
node $T/starter_match.mjs --site build/em-p/GeneralsMD --outcome defeat  --out /tmp/m1    # AI wins, score screen
node $T/starter_match.mjs --site build/em-p/GeneralsMD --outcome victory --out /tmp/m2    # player wins, score screen
sh   $T/shell_flows.sh   build/em-p/GeneralsMD /tmp/flows                                 # 15 platform / shell flows
```

* `starter_flow.mjs` clicks at the 800 x 600 design coordinates of the menus (`tools/wnd_pos.py <pack> <layout.wnd>` prints the
  centre of every button), prints the engine log after each step and takes screenshots (`s:NAME`). `--repl FILE` keeps the
  game open and runs the lines appended to FILE: that is how a match is explored by hand (look at a screenshot, append the next
  steps). `f:N` / `F:N` wait for logic frame N / N more frames, `W:REGEX` for a log line, `P` pauses the engine's threads
  and prints their call stacks (for a hang; the game thread is the one with `GameEngine` frames).
* `starter_match.mjs` writes its steps to `<out>/<outcome>.steps` (read them: they are the best description of what is played),
  runs them and fails on any assertion, missing string, wasm trap or audio error. `defeat`: the player does nothing against the
  Hard computer. `victory`: power plant, two barracks, twelve rocketeers (and twelve more that stay home) attack-move into the Easy computer's base. The picture
  of the 3D view (and so every click on the map) depends on the game's resolution and on the camera: the match starts at the
  default 1024 x 768 in the default 1100 x 800 window, picks start position 1 on the map preview, and presses Home (camera on the
  headquarters) and waits some frames before each order. The "next idle worker" key moves the camera to the worker.
* Under load a match takes 3 to 15 logic frames per second (30 is real time): wait on frames and log lines, never on seconds.
  Several browsers on one machine slow each other down; leftover servers keep their port (use another `--port`).
