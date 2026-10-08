# Handoff: the free starter content

For an AI coding agent or developer who has never seen the session that produced this. Read this first, then
`README.md` (how the pack is built) and `REQUIREMENTS.md` (what the engine needs from game data).

## Status and changelog (newest first; keep this section current)

* **2026-10-08, end of session 1.** Everything below "What exists" is in the tree. The pack builds
  deterministically (about 20 s), the 28 pack tests pass (about 100 s), the launcher e2e passes (70 checks), the engine
  boots with the pack to the point where it needs a working display (see "Known gaps"). **Not yet verified in a running
  engine:** the main menu on screen, the skirmish setup screens, loading the map, the AI, the control bar in play. The
  display path of the engine (WebD3D8) was still being brought up by another agent, so everything visual was verified
  only by parsing and by log analysis.

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
  team runs `IronwoodAttackWave` = `TEAM_HUNT`). The human side gets a script that starts battle music.
* Menus: main menu, skirmish options, skirmish map select, load screens, score screen, message box, quit menus, the
  control bar and its helper layouts (`gen/wnd_menus.py`, `gen/wnd_game.py`, DSL in `gen/wnd_widgets.py`), the interface
  art atlas and backdrops (`gen/ui_art.py`), about 190 strings (`gen/strings_data.py`).
* Audio: 23 effects and 4 music tracks synthesised in `gen/audio.py`, plus the audio INI files generated from the same
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
    wnd_widgets.py wnd_menus.py wnd_game.py strings_data.py textdb.py zz_strings.py misc.py
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

1. **"Use my Zero Hour installation"** (unchanged flow): the user picks the Zero Hour folder (and the Generals folder);
   `planImport` filters files (skips movies, `.exe` other than `generalszh.exe`, `.dll` ...), lower cases names and copies
   them into OPFS (`/game`, `/generals`) with a manifest (`kind: "install"`). **The engine fingerprints
   `generalszh.exe` at start up, so the importer keeps exactly that executable** (`TARGETS.game.engineExe`).
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

**A. Blocks reaching the main menu (engine side, not content)**

1. Display bring-up (WebD3D8/WW3D2): `Could not do WW3D::Begin_Render()`, vertex buffer assertion, no valid colour mode.
   Owner: the runtime agent / `Dependencies/WebD3D8`, `Core/GameEngineDevice`. Until this works nothing is visible.
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

## 12. Next 5 steps

1. Get the engine to draw frames (display bring-up, text rendering through the bundled font); then screenshot the main menu
   from the pack and fix whatever the layouts get wrong (section 9 B).
2. Add `add_subdirectory(Content)` to `cmake/emscripten.cmake` (section 6) so the pack is built next to the page, and run the
   launcher end to end with the real `z_generals` build and the starter download.
3. Drive the skirmish flow with Playwright (menu click, options, start) in a debug build, read the log, and fix content
   assertions (map loading, `SkirmishScripts.scb`, players, load screen, control bar).
4. Play a full match against the AI: check it builds, attacks and that victory/defeat reaches the score screen; tune costs,
   times, team sizes in `Object.ini`, `AIData.ini`, `gen/ai_scripts.py`.
5. Art: write `tools/gltf_to_w3d.py`, then replace models and textures one by one (section 11) and add unit animations.
