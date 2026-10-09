# What the engine needs from a game data set

This is the inventory behind the free starter content: every file and every template the engine looks for on the
way from a cold start to a finished skirmish, taken from this repository's own sources (never from game data).
Where a number or a name is fixed by the code, the source file that fixes it is named so the claim can be checked.

Legend

* **R**: required. Without it the engine throws, asserts or dereferences a null pointer before the player sees
  the menu, or the flow cannot continue.
* **F**: needed for the starter flow (main menu, skirmish setup, game, score screen) but the engine survives
  without it; the feature is just missing, or a debug assertion fires.
* **O**: optional. The pack does not ship it.

The pack root is the game install folder: in the browser the launcher mounts it at `/game` (the origin private file
system, lower case names, matched case insensitively). Loose files take precedence over archives, so a pack
can be a folder of files or a `.big` (`build_pack.py --big`).

Two tools keep this document honest and are run by the tests:

* `tools/engine_requirements.py <pack>` reads the engine sources for every INI folder it loads and reports the
  ones the pack does not provide.
* `tools/validate_pack.py <pack>` follows every reference (models, textures, images, strings, audio, templates,
  map, scripts) and reports the ones that do not resolve.
* `tools/check_wnd_names.py <pack>` lists the window names the code looks up that a layout lacks.

## 1. Executable and archives

| What | Status | Notes |
| --- | --- | --- |
| `generalszh.exe` in the install folder | F | `GlobalData::generateExeCRC` opens it to compute a checksum used by network version checks. Missing: an assertion in debug builds, a zero checksum otherwise. It is never run. The pack ships a short text file under that name; the web importer keeps the real executable of an installed game (it skips every other `.exe`). |
| `*.big` archives | O | The engine mounts every `*.big` it finds in the install folder (`StdBIGFileSystem::loadBigFilesFromDirectory`) and, through the registry shim, the Generals folder. Loose files win over archive entries. The pack uses loose files; `build_pack.py --big` writes one `StarterPack.big` for people who prefer an archive. Format: `tools/spk/bigfile.py`. |

## 2. INI files

`INI::loadFileDirectory(path)` loads `path.ini` and everything in the `path\` folder, and **throws
`INI_CANT_OPEN_FILE` when it finds no file at all** (`Core/GameEngine/Source/Common/INI/INI.cpp`). Every
`initSubsystem(..., "Data\INI\Default\X", "Data\INI\X")` call (`GameEngine.cpp`) therefore needs one file for
each of its two paths. A one line comment is enough for a name whose content is not needed. All 72 paths below
are **R**; `tools/engine_requirements.py` prints the current list.

Subsystems that load a `Default\X` file and an `X` file (both needed): `GameData`, `Water`, `Weather`,
`Science`, `Multiplayer`, `Terrain`, `Roads`, `PlayerTemplate`, `FXList`, `ObjectCreationList`, `SpecialPower`,
`Object`, `Upgrade`, `AIData`, `Crate`, `ShellMenuScheme`, `CommandButton` (+ `CommandSet`),
`ControlBarScheme`, `Video`, and the audio set `Music`, `SoundEffects`, `Speech`, `Voice`.

Loaded once: `Weapon`, `Armor`, `DamageFX`, `Locomotor`, `Rank`, `ParticleSystem`, `Animation2D`, `Mouse`,
`DrawGroupInfo`, `InGameUI`, `Eva`, `Campaign`, `ChallengeMode`, `Credits`, `Webpages`, `GameLOD`,
`GameLODPresets`, `WindowTransitions`, `ControlBarResizer`, `AudioSettings`, `MiscAudio`, `CommandMap`.

Language folder (`Data\english\`, the registry language): `Language.ini` (`GlobalLanguage`: fonts), `HeaderTemplate.ini`
(`HeaderTemplateManager`) and `CommandMap.ini` (`MetaMap`, key bindings, 76 entries the options and in game code use).
`MappedImages` come from the folders `Data\INI\MappedImages\HandCreated` and `Data\INI\MappedImages\TextureSize_<n>`
(`Image.cpp`).

What the content of the files must contain:

| File | Status | Must define |
| --- | --- | --- |
| `GameData.ini` | R | A `GameData` block. Numbers the code divides by or loops over (build speed, lighting for all four times of day, `NumberGlobalLights`), `MoveHintName` (the model of the move marker; `W3DInGameUI` asserts when it cannot create it), the auto fire/smoke particle names. Unknown fields assert in debug builds, so the parser table in `GlobalData.cpp` is the reference. |
| `PlayerTemplate.ini` | R | `FactionObserver` (the engine looks it up by name when a game starts), `FactionCivilian` (the neutral civilian side that the human player's scripts come from), and at least one playable faction: `Side`, `StartMoney`, `StartMoney` (0, so that the starting cash option of the setup screen decides: `Player.cpp` only uses the setup value when the template's is 0), `StartingBuilding`, `StartingUnit0..n`, the command sets for the science purchase windows, images, `LoadScreenMusic`/`ScoreScreenMusic`, `Features`, `ArmyTooltip`. The skirmish setup lists every template with `PlayableSide = Yes`. |
| `Object.ini` | R | Every template the other files name: starting building and units, what they build, scenery used by the map. See section 6. |
| `AIData.ini` | R (F for play) | `AIData` tuning values plus `SideInfo <Side>` and `SkirmishBuildList <Side>` for each playable side. The list starts with the command center, rotated 135 degrees by the engine (see the comment in the file); without it the computer opponent builds no base. |
| `Weapon.ini`, `Armor.ini`, `Locomotor.ini`, `DamageFX.ini`, `FXList.ini` | R/F | The blocks the objects name. A missing weapon makes the unit unarmed (debug assertion), a missing locomotor makes it immobile. `Locomotor.ini` must also define `BasicHumanLocomotor`: the rally point check of every production building looks it up by that name (`GameLogicDispatch.cpp`), and with none every rally point is refused ("Units cannot reach that place"). |
| `ParticleSystem.ini` | F | Systems the effect lists and `GameData` name; textures are in `Art\Textures`. |
| `CommandButton.ini`, `CommandSet.ini` | F | Buttons and sets for every command bar the starter units and buildings show; `ControlBar::init` loads them before anything else needs them. |
| `ControlBarScheme.ini` | R | At least one scheme (`Side`, `ScreenCreationRes`, the positions of the parts, `ImagePart` entries). The control bar picks the scheme of the local player's side by its `Side` and the screen resolution. |
| `ShellMenuScheme.ini` | R | One scheme whose images/lines the shell draws behind the menus. |
| `WindowTransitions.ini` | R | Every group a script, a layout or the code names with `reverse()`/`setGroup()`: an unknown group dereferences null (`GameWindowTransitions.cpp`). Empty groups are no-ops, so the pack defines every name that the shipped menu code mentions and leaves most of them empty. The quit menu relies on `QuitFull`/`QuitFullBack` fades. |
| `GameLOD.ini` | R | The four `StaticGameLOD` levels. `MaxTankTrackEdges` must not be 0: the level's value overwrites the global one and sizes the terrain-track vertex buffer (`W3DTerrainTracks.cpp`); a buffer with no vertices asserts at start up. Same for `MaxTerrainTracks` (`GameData.ini`), which has no usable default. |
| `Animation2D.ini` | F | The small animations the game draws over objects, by the fixed names of `TheDrawableIconNames` (`Drawable.cpp`: `DefaultHeal`, `StructureHeal`, `VehicleHeal`, `Demoralized`, `BombTimed`, `BombRemote`, `Disabled`, `BattlePlanIcon_*`, `Enthusiastic`, `Subliminal`, `CarBomb`). A missing one is a null template: an assertion for every such object on every frame (the first healed or damaged building did it). |
| `Mouse.ini` | R | The `Mouse` block with tooltip settings and one `MouseCursor` entry per cursor kind. Textures may be missing: the web build stubs cursors (`LoadCursorFromFile` returns a stock handle). |
| `DrawGroupInfo.ini`, `Rank.ini`, `Terrain.ini`, `Water.ini`, `Roads.ini` | R | `DrawGroupInfo`: font and offsets for the health bars. `Rank`: the pack defines ranks 1 to 5 (`RankInfoStore` is indexed by the rank number). `Terrain`: one entry per terrain texture class a map uses, plus `DefaultTerrain` (not enforced). `Water`: the four `WaterSet` blocks (time of day), `WaterTransparency`. |
| `AudioSettings.ini` | R | `AudioSettings` with the folders (`AudioRoot`, `SoundsFolder`, `MusicFolder`, `StreamingFolder`), `SoundsExtension`, volumes, ranges. `AudioManager::init` reads `m_audioSettings->m_preferred*Volume`. |
| `Default\SoundEffects.ini`, `Default\Music.ini`, `Default\Speech.ini` | R | The base events `DefaultSoundEffect`, `DefaultMusicTrack`, `DefaultDialog`: every later definition copies them before applying its own fields. |
| `MiscAudio.ini` | F | `MiscAudio` roles (radar warnings, GUI click, money, no-can-do ...). |
| Other loaded-once files (`Campaign`, `ChallengeMode`, `Credits`, `Eva`, `Video`, `Webpages`, `Animation2D`, `ControlBarResizer`, `InGameUI`, `Science`, `Upgrade`, `SpecialPower`, `Crate`, `ObjectCreationList`, `Multiplayer`, `Weather`) | R as files, content O | One line of comment each. The `Multiplayer` content (colours, starting money options) is what the skirmish setup offers; the starter pack relies on the engine's defaults. |

## 3. Strings

`GameTextManager` (`Core/GameEngine/Source/GameClient/GameText.cpp`) reads `Data\Generals.str` first and, when that
does not exist, `Data\<language>\Generals.csf`. The pack ships the text form.

* Line endings must be LF. With CRLF the parser keeps a carriage return in the label and reports
  `String label '' has more than one string defined!` for every entry (`tools/spk/strfile.py` writes LF).
* Labels used by code are looked up literally (`GUI:...`, `CONTROLBAR:...`, `LAN:...`, `MAP:...`); a missing label shows
  `MISSING: 'label'` and asserts in debug builds. `tools/validate_pack.py` checks every label the pack's own INI and WND
  files use; labels that code looks up by itself must be added by hand (the starter list in `gen/strings_data.py`).
* Format arguments in strings must use `%ls` for wide strings (the engine formats with `swprintf`).
* Labels the code builds by itself (not found by grepping for a literal): `NUMBER:<n>` (control-group digits, skirmish slot numbers),
  `SIDE:<Side of the player template>` (the faction combo box of the setup screens, `GUIUtil.cpp`: a faction added by another
  package needs its own), `Version:Format2/3/4`, `Version:BuildTime/BuildMachine/BuildUser` (replay list "Version" column),
  `GUI:Key_<NAME>` and `GUI:Key_<NAME>_Desc` for every `CommandMap` entry (`gen/keys.py`), `GUI:<CATEGORY>` for the key categories.
* A map names itself with the `mapName` entry of its world dictionary (`MAP:Name`); the label comes from
  `Maps\<map>\map.str`.
* Fonts: windows ask GDI for families by name (`CreateFont`). The pack names only `DejaVu Sans` (bundled, free licence,
  `Data\Fonts\`, listed by `LocalFontFile` in `Language.ini`). **Blocker for text:** the web platform layer
  currently stubs `CreateFontA`/`AddFontResourceA` (`Dependencies/WebCompat/src/win32_window.cpp`), so no glyphs are drawn
  until the font stubs render text (for example with FreeType and the bundled TTF files, or canvas 2D text).

## 4. Window layouts (`Window\...`)

`GameWindowManager::winCreateLayout` parses the `.wnd` script (`GameWindowManagerScript.cpp`); a missing file returns
null and the calling menu then dereferences it, so every layout the flow opens is **R** for that flow. Window names are
looked up by `"Layout.wnd:Name"` strings in code; `tools/check_wnd_names.py` extracts them from the sources and compares
them with the layouts. The starter pack ships:

| Layout | Status | Role |
| --- | --- | --- |
| `Menus\MainMenu.wnd` | R | First shell screen (`Shell::showShell`). Needs `MapBorder`..`MapBorder4` (the dropdown that `MainMenuInput` shows on the first input) and the hidden `*RecentSave`/`*LoadGame` buttons. |
| `Menus\SkirmishGameOptionsMenu.wnd`, `Menus\SkirmishMapSelectMenu.wnd` | F | Skirmish setup: slots, player template combo boxes, map selection (`GameWindowManager` gadget messages go to the owner, so panels pass messages to their parent). |
| `Menus\ShellGameLoadScreen.wnd`, `Menus\MultiplayerLoadScreen.wnd` | F | The load screens (`LoadScreen.cpp`: skirmish uses the multiplayer one). |
| `ControlBar.wnd`, `ControlBarPopupDescription.wnd`, `GeneralsExpPoints.wnd`, `ReplayControl.wnd` | R for a match | The in-game bar. 18 command buttons `ButtonCommand01..18` (must be push buttons with the IMAGE status, because `setControlCommand` rejects anything else), the queue slots, `LeftHUD`, `RightHUD`, `PowerWindow`, `MoneyDisplay`, observer panels. The scheme file moves some of them (positions in `ControlBarScheme.ini` override the layout). |
| `Menus\ScoreScreen.wnd` | F | End of match. |
| `Menus\MessageBox.wnd`, `Menus\QuitMenu.wnd`, `Menus\QuitMessageBox.wnd`, `Menus\QuitNoSave.wnd`, `Menus\BlankWindow.wnd` | F | Dialogs the shell and the in-game menu open. |
| `Menus\OptionsMenu.wnd`, `KeyboardOptionsMenu.wnd`, `CreditsMenu.wnd`, `SaveLoad.wnd`, `PopupSaveLoad.wnd`, `ReplayMenu.wnd`, `PopupReplay.wnd`, `Victorious.wnd`, `Defeat.wnd`, `LocalDefeat.wnd`, `ObserverQuit.wnd`, `InGameChat.wnd`, `Diplomacy.wnd`, `InGamePopupMessage.wnd`, `Lan*.wnd` | F | The remaining screens of the menu flow (`gen/wnd_extra.py`, `gen/wnd_lan.py`). The banners are created by the victory / defeat script actions (`ScriptActions.cpp`). |
| Everything else the code can open (campaign, GameSpy, challenge ...) | O | The starter menu does not offer these screens. |

Facts about layouts that cost time (all enforced by `gen/wnd_widgets.py` / `tools/spk/wnd.py`, with tests):

* A window listed later in a layout draws on top of the earlier ones and is hit-tested first. The drop-down list of a combo box
  opens below it and extends over other windows, so `tools/spk/wnd.py` writes the combo boxes of a parent last, the lowest one
  first. Without this only the first rows of a list are visible and clickable.
* A check box (`W3DCheckBox.cpp`) draws its box a sixteenth of the width from the left, one third of the height wide, and its label
  one height from the left edge: a window wider than `16 * (2/3 height - 4)` draws the box over the label. `checkbox()` fixes the
  height and clamps the width; the label may run past the right edge (the background is transparent).
* A static text whose label contains `&` before a hot key letter needs the status `HOTKEY_TEXT` (the tooltip title of a command
  button does), or the `&` is printed.
* The production queue strip (`ProductionQueueWindow`) is shown by the engine next to the command buttons while a building is
  producing; it must not overlap `CommandWindow`, or the buttons cannot be clicked again and nothing more can be queued.
* `populateSkirmishBattleHonors` fills the wins / losses numbers of the skirmish setup only if the layout has a `ListboxInfo`
  list (hidden in the pack).
* The `MultiplayerScripts.scb` victory / defeat scripts exist for the banners (see section 9).

Each layout names callbacks (`SYSTEMCALLBACK = "MainMenuSystem"`); they are resolved through `FunctionLexicon` tables that
are compiled into the engine, so only those names are valid. Gadget windows use `STYLE = PUSHBUTTON+MOUSETRACK` and the
nine draw-data entries per state (`MAX_DRAW_DATA`); `TEXT` must be a non-empty string label for gadgets that show text.

## 5. Images and textures

* `Data\INI\MappedImages\HandCreated\*.ini` define named sub-rectangles of textures (`MappedImage`); windows, buttons,
  schemes and templates refer to those names. The pack has one atlas (`Art\Textures\sp_ui.tga`) plus backdrops.
* `Art\Textures\*.tga` (or `.dds`). Hard coded names the pack also ships: `shadow` (decal shadow), `TBBib`, `TBRedBib`
  (base bibs), `TSNoiseUrb`, `TSCloudMed`, `cloudmap`, `EXScorch01`, `alphaclip`, `TSMoonLarg`, `Noise0000`, `TWAlphaEdge`,
  `WaterSurfaceBubbles`, `EXLaser`, `wave1`, `wave2`, `wave256` and `missing` (the code falls back to a built in
  placeholder, but the bundled one avoids a log warning).
* `Art\Terrain\<name>.tga`: terrain texture sheets named by `Terrain` blocks. A multiple of 64 pixels in both directions;
  a 128 x 128 sheet holds 2 x 2 tiles of 64 pixels (`WorldHeightMap::readTiles`). The reader ignores the TGA id field and
  the origin flag, and RLE runs must not cross a scan line, so the pack writes id length 0 and per-line runs.
  Terrain tiles are 64 pixels with four 32 pixel cells each.
* TGA: types 2 and 10 (RLE), 24 or 32 bits. `tools/spk/tga.py` writes both; the alpha channel is kept only when asked.

## 6. Objects (what a template must contain)

An `Object` block is parsed by `ThingTemplate` and the modules it lists; unknown fields assert in debug builds. The
starter pack's templates are the practical reference (`data/Data/INI/Object.ini`). What the game logic insists on:

* `Side` equal to the `Side` of the player template; `KindOf` flags that the rules read: `STRUCTURE`, `SELECTABLE`,
  `COMMANDCENTER` (the building the AI places first and the start of a match), `MP_COUNT_FOR_VICTORY` (the structures that keep a
  player alive: `VICTORY_NOBUILDINGS` in `VictoryConditions.cpp`), `DOZER` (the builder), `INFANTRY`, `VEHICLE`, `CAN_ATTACK`,
  `SCORE`, `SHRUBBERY` for scenery that is not a prop.
* `Draw = W3DModelDraw` with a `DefaultConditionState` naming a model (`Art\W3D\<Model>.w3d`, 15 characters at most) and
  `OkToChangeModelColor = Yes` when the player's colour should show (meshes named `HOUSECOLOR*` are recoloured).
* `Body = ActiveBody` (health), `ArmorSet`, `WeaponSet`, `Behavior = AIUpdateInterface` (units) or `DozerAIUpdate` (workers),
  `ProductionUpdate` with `CommandSet` for factories, `AutoDepositUpdate` for the income of the starter economy,
  `Locomotor = SET_NORMAL <Name>`, `Geometry*` for selection and collision, `Shadow`, `VisionRange`, `ShroudClearingRange`.
* Combat units: `Behavior = AIUpdateInterface` with `AutoAcquireEnemiesWhenIdle = Yes Attack_Buildings` (`AIUpdate.cpp`
  `getNextMoodTarget`): without the second word idle units and units on attack move ignore enemy structures.
* `Prerequisites` (what must stand first), `BuildCost`, `BuildTime`, `ButtonImage`/`SelectPortrait`, `DisplayName` (a string label).
* Production buildings need `UnitCreatePoint`/`NaturalRallyPoint` and `MaxQueueEntries`.

## 7. 3D art

`Art\W3D\<Name>.w3d`: chunk based files (`tools/spk/w3d.py` documents the layout). A model has a hierarchy (one bone is enough),
one or more meshes and an HLOD whose sub-object names are `<model>.<mesh>`. Names are limited to 15 characters. The engine
creates a few models by name in code: `Locater01`/`Locater02` (building placement anchor and arrow), `SCMNode` (waypoint
node), `new_skybox` (the sky box of the water renderer) and whatever `MoveHintName` says. A missing model makes
`Create_Render_Obj` return null: a debug assertion for the ones above, an invisible object for templates.

## 8. Audio

* Sound effects: `AudioEvent Name` blocks (`Sounds = a b`, volumes, ranges, `Type`, `Control`); the files are
  `<AudioRoot>\<SoundsFolder>\<name>.<SoundsExtension>`. Music: `MusicTrack Name` with `Filename = file.wav` in
  `<AudioRoot>\<MusicFolder>\`. Speech (`DialogEvent`) is optional and not shipped.
* Format: RIFF/WAVE, PCM 8/16/24/32 bit, float, or Microsoft IMA ADPCM (`WebAudioDecoder.cpp`). The pack uses 16 bit PCM
  for effects and IMA ADPCM for music.
* Event names the engine looks up itself: the `MiscAudio` roles, `GUIClick`, `GUIClickDisabled`, `GUICommandBarClick`,
  `GUIComboBoxClick`, `GUIBlip`, `GUIMessageReceived`, `GUITypeText`, the per-unit `Voice*`/`Sound*` fields, and the
  music names in the player template. An unknown event is ignored (a debug log line).

## 9. Maps

`Maps\<name>\<name>.map` is a DataChunk file (`tools/spk/mapfile.py` documents every chunk). `MapCache`
scans `Maps\*\*.map` and keeps `MapCache.ini` in the user data folder; a map is offered in the skirmish list when it has
at least two `Player_N_Start` waypoints.

Required chunks, in this order: `HeightMapData` (v4), `BlendTileData` (v8), `WorldInfo`, `SidesList` (with the script
lists), `ObjectsList`, `PolygonTriggers`, `GlobalLighting`, `WaypointsList`. `WorldInfo` must precede `SidesList`, and
`SidesList` must precede `ObjectsList`. A terrain cell is 10 world units; the border cells are outside the playable
area; heights are bytes scaled by 0.625. Tile indices are `(tile << 2) + 2*(y&1) + (x&1)`.

What a skirmish map needs:

* Waypoints `Player_1_Start`, `Player_2_Start`... (objects with a `waypointID` and `waypointName`); the game puts the
  starting building and units there, and the camera starts there.
* Sides: the neutral side (name empty), a civilian side whose faction is `FactionCivilian`, and for every playable
  faction a skirmish side `Skirmish<Faction>` whose `playerFaction` is the template. `SidesList::prepareForMP_or_Skirmish`
  removes all sides except the civilian one, the setup screen then creates `player0...` sides from the slots, and
  `Player::initFromDict` copies the scripts and teams of the skirmish side whose template has the same `Side`.
* Scripts for the opponent: either in the map's own side script lists or in `Data\Scripts\SkirmishScripts.scb`
  (`ScriptsPlayers`, `PlayerScriptsList`, `ScriptTeams`). Script, team and counter names get the start position index
  appended, `SIDE` parameters naming the template side are replaced with the real player name. Condition and action ids
  are positions in the enums of `Scripts.h`; the argument lists are the templates in `ScriptEngine.cpp`
  (`tools/spk/scripts.py` keeps the subset used and the tests compare it with the sources).
* AI teams (`ScriptTeams`): `teamName`, `teamOwner`, `teamUnitTypeN`/`teamUnitMinCountN`/`teamUnitMaxCountN`,
  `teamMaxInstances`, `teamProductionPriority`, `teamProductionCondition` (the name of a script: with none the team is never
  built), `teamOnCreateScript` (a subroutine script run when the team is complete; `TEAM_HUNT` sends it to the enemy).
  The AI also needs a factory that can train each unit and the money for it.
* Objects: every name must be a template (unknown names are skipped); owners are `originalOwner = "team"` for neutral
  scenery. Waypoint objects use the template name `*Waypoints/Waypoint`.
* `Data\Scripts\MultiplayerScripts.scb` is **needed to end a game**: `VictoryConditions` only tracks who is still alive, the
  scripts `MultiplayerVictory` (`MULTIPLAYER_ALLIED_VICTORY` -> `VICTORY`), `MultiplayerDefeat` (`MULTIPLAYER_ALLIED_DEFEAT` ->
  `DEFEAT`) and `MultiplayerPlayerDefeat` (`MULTIPLAYER_PLAYER_DEFEAT` -> `LOCALDEFEAT`) show the banner and send the game to the
  score screen (`GameLogic.cpp` appends them to the neutral side of every game with two or more teams; `gen/mp_scripts.py`).
* A computer opponent also needs build orders in its skirmish scripts: the `SkirmishBuildList` of `AIData.ini` is only followed
  for the first building and for rebuilding; the barracks and the factory come from `SKIRMISH_BUILD_BUILDING` actions
  (`gen/ai_scripts.py`), and `TeamResourcesToStart` in `AIData.ini` must be small (1.0): it is the money a team needs before
  the AI starts to build it, and with 100 no team was ever started.
* Preview: `Maps\<name>\<name>.tga` shown in the setup screen (optional).

## 10. Behaviour of the web build that the content has to live with

* The engine starts in `/game` (OPFS). Case does not matter, separators do not matter. The directory `/userdata` holds
  options, the map cache and save games, created on demand.
* `generalszh.exe` has to exist for the exe check (section 1).
* The display, text and cursor layers are platform code: see the report to the coordinator for the current blockers
  (fonts, cursors, D3D device creation) which no data can work around.
* Memory pools and the INI parser are strict: an unknown field aborts loading in debug builds, so the pack is developed
  against a debug engine (`RTS_WEB_DEBUG`), where every typo shows up in the first boot log.
* The assertions of the engine (`ASSERTION FAILURE` in the console log) are compiled in with
  `cmake --preset emscripten -B build/em-p -DRTS_DEBUG_LOGGING=ON -DRTS_DEBUG_CRASHING=ON`. A clean pack gives none in the menus or
  in a whole match (`GeneralsMD/Code/Main/web/test/starter_match.mjs` fails on any). Known exception that is not content: with a
  browser window smaller than the game's resolution (1024 x 768) and the launcher's "As set in the game's Options", the renderer
  reports `No valid texture format found` at start up (platform code).
