# Free starter content

An original, free placeholder game for the WebAssembly port: one made-up faction (the **Ironwood Compact**: a
headquarters, a power plant, a barracks, a factory, a worker, two infantry units and two vehicles), one two player
skirmish map ("Ironwood Crossing"), the menus that lead there and the in-game control bar, with sounds and music.
It lets the engine boot to a main menu and play a small skirmish against the computer opponent without anyone's
copy of the original game. **It is not Command & Conquer content and does not try to look like it.**

Everything here is generated from source by Python 3 scripts (no third party packages), is licensed
**GPL-3.0-or-later**, and nothing is copied from the original game's data. The file formats and the names the engine
asks for were derived only from this repository's GPL source code; `REQUIREMENTS.md` lists them with the source file that
fixes each one.

```
python3 build_pack.py <out>            # writes <out>/StarterPack/ (loose files + manifest.json), about 20 seconds
python3 build_pack.py <out> --big      # also writes <out>/StarterPack.big
python3 -m unittest discover -s tools/tests          # formats, content, determinism, cross references
python3 tools/validate_pack.py <out>                 # follow every reference in a built pack
python3 tools/engine_requirements.py <out>           # INI folders the engine loads vs the pack
python3 tools/check_wnd_names.py <out>               # window names the code looks up vs the layouts
python3 tools/preview_w3d.py sheet.png a.w3d b.w3d   # contact sheet of models (needs Pillow, developer convenience)
```

The build is deterministic: the same sources give byte identical files (fixed seeds, no timestamps).

In the CMake build the web target `starter_pack` (see `Content/CMakeLists.txt`) generates the pack into the
`starterpack/` folder next to `z_generals.html`. The launcher's second option, "Play with free starter content",
downloads `starterpack/manifest.json` and every file it lists into the browser's file system (OPFS, `/game`) and
starts the game from there.

## Layout

| Path | What |
| --- | --- |
| `build_pack.py` | collects `data/` and everything the generators emit, writes the tree and `manifest.json` |
| `data/` | hand written text files laid out like an install folder (INI files, English `Language.ini`...) |
| `gen/` | generators: `textures` (terrain, particles, fixed name textures), `ui_art` (interface atlas, backdrops), `models` (W3D), `audio` (WAV, IMA ADPCM, audio INI), `maps` (the map), `ai_scripts` (`SkirmishScripts.scb`), `wnd_*` (window layouts), `strings_data` (the string table), `misc` (placeholder executable, font) |
| `tools/spk/` | the writers (and readers used by the tests): BIG, TGA, WAV/ADPCM, STR/CSF, W3D, WND, DataChunk, map, script chunks, a software synthesiser, a canvas |
| `tools/tests/` | unit tests: round trips, structure checks against the engine's own rules, determinism |
| `third_party/dejavu/` | DejaVu Sans and Sans Bold with their licence (Bitstream Vera licence, redistribution allowed) |

## Editing the game

* Gameplay numbers live in `data/Data/INI/*.ini` (`Object.ini`, `Weapon.ini`, `AIData.ini`...). Unknown fields make a debug engine assert on
  startup, so boot a debug build after each change.
* Strings: `gen/strings_data.py`. Menus: `gen/wnd_menus.py` and `gen/wnd_game.py` (a small DSL in `gen/wnd_widgets.py`).
* Models are built from primitives in `gen/models.py` and use one palette texture; preview them with `tools/preview_w3d.py`.
* The opponent: `SkirmishBuildList` in `AIData.ini` (what it builds and where) and `gen/ai_scripts.py` (what it trains and when it attacks).

See `NOTICE.md` for the licences of the bundled font.
