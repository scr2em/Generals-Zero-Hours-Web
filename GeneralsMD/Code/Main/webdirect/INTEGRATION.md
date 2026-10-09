# Read in place: play from the player's own folder, no copy

Status: implemented, wired into the launcher (`web/shell.html`, `web/importer.js`) as the default way to use
"my Zero Hour installation", tested in headless Chromium. The copy into browser storage stays as an option.

## Why

The launcher used to copy the install (about 2.2 GB for Zero Hour plus the original game) into the Origin Private File
System. That fails when Chrome's quota is small ("need 1.21 GB, 1.09 GB available") and is slow on the first run. Now the
engine reads the files straight from the folder the player picked.

## Design, and why this one

```
 page (main thread)                              engine pthread (WasmFS)
 -----------------                               ------------------------
 showDirectoryPicker() handle  --IndexedDB-->    (remembered between visits)
 walk + lower-case + importer's selection rules
 handle.getFile() per needed file  = File objects (references, nothing is read)
   Module.preRun -> Module.zhDirectAttach(payload)
        postMessage({zhDirect}) to every pool worker  ---->  Module.zhDirect (zhdirect_pre.js)
                                                             WebPlatform_MountStorage() (WebStorage.cpp):
                                                               JSImpl backend + ignore-case wrapper at /direct,
                                                               one empty file per entry, /game -> /direct/game
                                                             read(2) -> JS callback (library_zhdirect.js):
                                                               FileReaderSync on the File, block cache (LRU)
                                                               -> bytes into the wasm heap
```

Options considered:

* **(a) WasmFS JSImpl backend + `FileReaderSync` on transferred `File` objects** (chosen). Synchronous, runs on whichever
  thread calls `read`, needs no main thread round trip, uses only public WasmFS API (`wasmfs_create_jsimpl_backend`,
  `wasmfs_create_icase_backend`), memory is bounded by our cache. Files are structured-cloneable; the engine thread and the
  audio stream thread (which reads archives, too) each get them.
* (b) legacy WORKERFS: it is an Emscripten FS (not WasmFS) backend; with `-sWASMFS` it does not exist. Porting it
  would be (a) anyway.
* (c) Main thread serves reads through a SharedArrayBuffer and `Atomics.wait` on the engine thread: works, but every read
  is a round trip through a main thread that may be busy, and it needs a protocol of our own. (a) is simpler and faster.
* (d) A custom C++ WasmFS backend: needs Emscripten's internal headers; JSImpl is the supported seam.

Key facts: `FileReaderSync` has a fixed cost per call (a synchronous round trip to the browser process: about 2 ms in the
headless Chromium of the sandbox, expected much less on a desktop), while the game reads archives in many small pieces. So
reads are served from block aligned **segments** (256 KB for random access, doubling up to 4 MB while a file is read
sequentially), kept in an LRU (64 MB for the engine thread, 16 MB for each other thread). Reads of 1 MB or more bypass the
cache and go straight into the heap. Nothing ever reads a whole archive.

## Files

| File | What |
|---|---|
| `webdirect/direct-source.js` | page side: folder handles in IndexedDB, permission, import mode `'direct'`, `attachCurrent(Module)` |
| `webdirect/zhdirect_pre.js` | `--pre-js`: posts the File objects to the workers (main thread) / receives them (workers) |
| `webdirect/library_zhdirect.js` | `--js-library`: the JSImpl backend with the read cache |
| `Core/GameEngineDevice/Source/WebDevice/Platform/WebStorage.cpp` (+ `WebPlatform.h`) | mounts `/game`, `/generals` from the direct tree or from OPFS; `/userdata` always OPFS |
| `GeneralsMD/Code/Main/WebMain.cpp` | `-webdirect` (fail with a clear message instead of falling back to OPFS), `-webdirectstats` (logs read counters) |
| `GeneralsMD/Code/Main/CMakeLists.txt` | the two link inputs, copies `direct-source.js` next to the page |
| `web/importer.js` | small changes: `sourceFromDirectoryHandle` keeps `.handle` and the walk's `File`s; the manifest records `mode` |
| `web/shell.html`, `web/test/e2e.mjs` | the launcher flow below; e2e runs the copy flows with `copy=1` and has the new direct section |
| `webdirect/test/*` | tests, see `test/README.txt` |

## How the launcher uses it (already done; this is what to keep when editing)

1. `shell.html` imports `./direct-source.js` next to `./importer.js` (both are copied next to `z_generals.html`). Importing it
   registers the import mode `'direct'` (`registerImportMode`).
2. **Default flow** ("Select folder..."): the same steps as the copy (`pickSource` -> `detectInstall` -> `planImport`), but
   `plan.mode = 'direct'`: no quota check, no storage persistence request, no duplicate skipping, and `runImport` only
   resolves a `File` per planned file and writes the OPFS manifest (`mode: 'direct'`). The picked handle is remembered with
   `direct.rememberFolder(role, source.handle)` (roles `'game'` for the Zero Hour pick, `'generals'` for the optional second pick).
3. **Copy option**: the checkbox `#copy-mode` ("Copy the files into this browser's storage instead", remembered in
   `localStorage` `zh-copy-mode`; `?copy=1` forces it; without `showDirectoryPicker` and without `?picker=` it is the only way).
   Switching between the two removes what the other left behind (the copy, or the remembered folder).
4. **Next visit** (`refreshStatus`): a manifest with `mode: 'direct'` and nothing open (`direct.isOpen('game')` false) means
   permission is needed. `direct.queryAccess(saved)`: `'granted'` (Chrome kept the permission) opens the folder without a
   click; otherwise the button reads "Allow access" and its click handler calls `direct.requestAccess(saved)` as its first
   action (it needs the user gesture), then `reopenSaved()` runs the same import code on the saved handles. "Choose a different
   folder" is offered next to it.
5. **Play** (`startGame`): after `window.Module = {...}` and before the game script is added:
   `if (status.game.mode === 'direct') direct.attachCurrent(window.Module);`
   It adds a `preRun` step (hands the files to the engine threads) and `-webdirect` to `Module.arguments`.
6. "Remove imported files" also calls `direct.closeOpen()` and `direct.forgetFolders()`.

Without the launcher's plan flow, the one-call API is `openInstall(foldersOrFileList)` -> `{ game, generals, warnings, attach(Module) }`,
`prepareDirect({ folders })` for a click handler, `pickFolder()`, `loadFolders()`, `queryAccess()`, `requestAccess()`.
`webdirect/test/direct_boot.html` is a minimal page that uses them.

C++ switch: none for the launcher. `WebPlatform_MountStorage()` mounts the direct tree when `Module.zhDirect` was delivered, else the
OPFS copy. `-webdirect` in `Module.arguments` makes a missing delivery a clear error. Build flags: nothing to add by hand; both
JS files are link inputs in `zh_web_link_options` (the game, `web_platform_test`, `web_fs_test`, `web_direct_test`).

Which files are opened is exactly the copy importer's selection (`planImport`): names lower-cased, no movies / exe (except the one
the engine fingerprints) / dll, archives only at the top level, `maps.big` of the original game skipped. Read in place has no
space cost, so nothing is de-duplicated between the two installs.

## Army packages at /armies

The launcher's "Armies" section (`web/armies.js`, `web/shell.html`) serves the army packages (`*.zharmy`, see
`docs/ARMY_PACKAGES.md`) the player ticked through the same backend, in every mode (read in place, copy in OPFS, starter content):

* `direct.setArmyFiles([{ name, file }])` adds the entries `armies/<name>` to the payload. With the game read in place
  `attachCurrent()` sends them together with the game files; otherwise `attachArmies(Module)` sends a payload that holds only them
  (no `-webdirect`, the game data still comes from OPFS).
* `WebStorage.cpp` (`zh_direct_roots()` tells which roots the entries are below) mounts the tree at `/direct` as before, creates
  `/direct/armies` and links it at `/armies`. `/game` and `/generals` are linked to `/direct` only when game files were handed over.
  The names are lower case in the tree and found without regard to case; the launcher passes `-army /armies/<name>` with the name
  it served (the file's own name when it only uses letters, digits and `. _ ( ) + -`, else a plain version of it; sub folders are kept).
* The armies folder handle is kept in IndexedDB under its own key (`rememberArmiesFolder`, `loadArmiesFolder`,
  `forgetArmiesFolder`), separate from the game folders.

Armies imported in the browser (`web/armylibrary.js`, `web/armyimport-ui.js`) are `.zharmy` files in the OPFS folder
`armies-library/`. They need nothing from this backend: the launcher takes the `File` of each ticked one (`getFile()` of the OPFS
handle) and passes it to `setArmyFiles` with the name `library/<id>.zharmy`, so the engine reads it at
`/armies/library/<id>.zharmy` like a package from a folder. `openFiles(targetKey)` returns the files of an open target
(`[{ path, file }]`); the importer uses it to compare a mod with the Zero Hour files that are read in place.

## Test results (headless Chromium in the sandbox, `webdirect/test/run_all.sh`)

* Backend test (`web_direct_test`): mount, case insensitive lookup (lower/mixed/upper, relative after `chdir`), directory
  listing, `stat` sizes, sequential/odd-sized/random reads, `lseek` incl. past the end, `fseek`/`fread`, 3 MB direct read, reads
  across blocks and the end of file, writes refused (EACCES/EROFS), `/userdata` writable, random reads from 3 other threads: all pass.
* A file changed on disk after opening: the read fails with EIO and the page's error panel gets
  "Cannot read generals/ini.big: NotReadableError ... Open the folder again".
* Dummy folder with empty archives: the engine stops with "Required game file Data\INI\Default\GameData.ini was not found."
* Starter pack loose (178 files), through the real launcher and the real game: main menu, skirmish setup, map loads, base,
  workers, control bar: `scratchpad` screenshots `launcher-direct.png`, `direct/skirmish-game2.png`, `shots/menu.png`.
* Start-up, first game frame after the game script is added (median of 3, load average 3-6 on 4 cores, so noisy):
  direct 1.1 s vs OPFS 2.4-3.2 s (OPFS also needed a 2.0 s copy of 5.5 MB). The same pack as one 5.5 MB archive:
  direct 0.9 s, OPFS 1.5-1.8 s (+0.2 s copy). Reads before the first frame: 595 reads, 100 fetches (loose); 223 reads, 10 fetches (archive).
* 1.5 GB file read completely (sequential 64 KB reads, verified byte for byte) plus random reads: wasm heap constant at 512 MB;
  browser RSS 742 MB at start, peak about 1000 MB, 927 MB at the end (cache 64 MB + garbage collector slack; with three more
  reading threads about 1030 MB). Throughput 90-120 MB/s here, which is the browser's own limit; cold random 256 KB block ~2.9 ms.
* 4000 loose files: page opens them in 1.25 s (`getFile` per file), the engine mounts them in 104 ms.
* Launcher e2e (`web/test/e2e.mjs`): 118 checks pass including the starter flows, the copy flows and the new direct flows
  (no copy in OPFS, Allow access with a stubbed permission, next visit without a click, switching copy <-> direct).

Not testable headlessly: the real `showDirectoryPicker` and its permission prompt. The tests use `<input webkitdirectory>` `File`s
(the same kind of object `FileSystemFileHandle.getFile()` returns) and an OPFS directory handle (a real `FileSystemDirectoryHandle`,
stored in and loaded from IndexedDB, walked and read) in its place. **Please try once on desktop Chrome with a real folder.**

## Known limits

* Chrome only (`showDirectoryPicker`). Permission: Chrome asks again on each visit unless the player chooses "Allow on every
  visit"; the page says so. Permission is only needed while opening the folder: the `File` objects stay readable during the visit.
* The files must not change while the game runs (a `File` is a snapshot; a changed or moved file fails with EIO and a message).
  Close the game and open the folder again. Installing a patch while playing is not supported.
* The folder is read-only for the game: anything the game writes next to its files fails (it writes to `/userdata`, in OPFS).
  Files of 2 GiB or more are skipped with a message (the JSImpl backend reports sizes as 32 bit); no game archive is that big.
* The audio stream thread and the engine each have their own cache, so a file both read is fetched twice. Threads that Emscripten
  creates beyond the pool of 6 are also given the files (the page wraps `PThread.allocateUnusedWorker`; not exercised by a test).
* Random access to cold data costs about one `FileReaderSync` round trip per 256 KB block (~2 ms in the sandbox); loading an
  asset-heavy map from a cold start is slower than from OPFS' synchronous handles in the best case, faster at start-up (no copy,
  and the engine's own OPFS proxy hops are gone). Tunable through `attachCurrent(Module, { blockSize, cacheBytes, ... })`.
* `Module.arguments` is read by the engine; `-webdirect` must come from `attachCurrent` (it does).
