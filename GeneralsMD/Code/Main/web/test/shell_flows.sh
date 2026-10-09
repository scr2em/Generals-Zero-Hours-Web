#!/bin/sh
# The platform and game shell flows of the web build, driven through the real game with the free starter content in
# headless Chromium (software WebGL, so a frame takes a while: allow ten minutes).
#
#   source emsdk_env.sh
#   cmake --preset emscripten -B build/em-p && ninja -C build/em-p z_generals starter_pack
#   PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers NODE_PATH=/opt/node22/lib/node_modules \
#       GeneralsMD/Code/Main/web/test/shell_flows.sh build/em-p/GeneralsMD [out dir]
#
# Flows (each is one starter_flow.mjs run, see its header for the step language; it exits 1 on a wasm trap or a failed
# expectation):
#   exit        main menu -> Exit -> Yes ends in the launcher's "game ended" page; Play again starts the game again
#   quit        the page's Exit button and a second close request quit from inside a match
#   save, load, loadmenu   save a skirmish; in a new page load it from the match and from the main menu (saves are in OPFS)
#   replay      a skirmish leaves a replay that the Replays menu lists and plays
#   options     the resolution, brightness, music and scroll speed set in Options are in Options.ini and survive a reload
#   window      resize, fullscreen, hidden tab (the game and the audio pause), screenshot button, edge scroll, pointer leave
#   cursors     the game's .ANI cursors are loaded, animated and have their hot spot (needs make_ani.py's cursors)
#   crash-*     an out of memory, a bad INI, a wasm trap and an abort in the engine are reported with a way on
#   sharp       device pixel ratio 2 with "Fit this window, sharp": one canvas pixel per screen pixel
#
# The coordinates are those of the 800x600 design space of the starter pack's menus mapped onto the canvas
# (tools/wnd_pos.py in Content/StarterPack finds the centre of a button).
site=$(cd "$1" && pwd)
out=${2:-${TMPDIR:-/tmp}/zh-shell-flows}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
failed=0
flow="node $here/starter_flow.mjs --site $site --port ${PORT:-8951}"

# main menu
SKIRMISH="c:185,221"; LOADGAME="c:185,317"; REPLAYS="c:185,366"; OPTIONS="c:185,413"; EXIT="c:185,509"
START="c:699,566"; BACK="c:103,566"
# in game: the Menu button and the pause menu
MENU="c:515,449"; PAUSE_OPTIONS="c:399,268"; PAUSE_SAVELOAD="c:399,316"; PAUSE_EXIT="c:399,412"
# dialogs
YES="c:318,368"; CONFIRM_YES="c:312,330"; OK_SAVE="c:302,350"; OK_REPORT="c:699,566"
NEW_SAVE_ROW="c:65,100"; SAVE_BUTTON="c:124,502"; FIRST_SAVE_ROW="c:92,116"; LOAD_BUTTON="c:326,502"
REPLAY_ROW="c:62,100"; REPLAY_PLAY="c:124,502"
# the game in play (the skirmish needs about 25 s to load with software rendering)
IN_GAME="w:3 $SKIRMISH w:3 $START w:30"

# FLOWS="options cursors" runs only those.
run() {
	name=$1; steps=$2; shift 2
	if [ -n "$FLOWS" ]; then case " $FLOWS " in *" $name "*) ;; *) return ;; esac; fi
	rm -rf "$out/$name"; mkdir -p "$out/$name"
	if $flow --out "$out/$name" --log "$out/$name/log.txt" --steps "$steps" "$@" > "$out/$name/out.txt" 2>&1; then
		echo "PASS $name"
	else
		echo "FAIL $name  (see $out/$name/out.txt)"
		grep -E "FAIL|RESULT|expect " "$out/$name/out.txt" | head -8
		failed=$((failed + 1))
	fi
}

# 1. Main menu -> Exit -> confirm: no trap, the page shows the ended state, Play again restarts without a click.
run exit "w:3 $EXIT w:2 $YES w:4 s:ended \
	e:__zh.state().gameEnded===true&&__zh.state().gameRunning===false \
	e:document.getElementById('ended').hidden===false&&/has~ended/.test(document.getElementById('ended-title').textContent) \
	e:document.getElementById('errors').hidden===true \
	q:#play-again w:3 g w:3 e:__zh.state().gameRunning===true N:RuntimeError"

# 2. The page's Exit button asks the game to quit (the game shows its menu); a second request from inside a match
#    quits to the launcher.
run quit "$IN_GAME q:#quit w:4 s:quit-menu e:__zh.state().gameRunning===true \
	v:window.Module._WebPlatform_RequestClose()&&1 w:8 e:__zh.state().gameEnded===true"

# 3. Save, reload the page, load. (The first run keeps the browser profile, the second one reads it.)
[ -z "$FLOWS" ] || case " $FLOWS " in *" save "*) ;; *) false ;; esac && rm -rf "$out/profile-saves"
run save "$IN_GAME $MENU w:4 $PAUSE_SAVELOAD w:4 $NEW_SAVE_ROW w:1 $SAVE_BUTTON w:3 $OK_SAVE w:4 s:saved \
	e:__zh.importer.listUserData().then(l=>l.some(f=>/\/save\/.+\.sav$/.test(f.path)))" --profile "$out/profile-saves"
run load "$IN_GAME $MENU w:4 $PAUSE_SAVELOAD w:4 s:list $FIRST_SAVE_ROW w:1 $LOAD_BUTTON w:3 $CONFIRM_YES w:40 s:loaded \
	N:could~not~be~loaded N:Error~loading~block" --profile "$out/profile-saves"

# 3b. Load game from the main menu (no match running).
run loadmenu "w:3 $LOADGAME w:5 $FIRST_SAVE_ROW w:1 $LOAD_BUTTON w:3 w:45 s:loaded N:could~not~be~loaded N:Error~loading~block" --profile "$out/profile-saves"

# 4. A short match, back to the menu through the pause menu, the battle report, the replay menu and the replay.
run replay "$IN_GAME $MENU w:4 $PAUSE_EXIT w:4 $YES w:8 $OK_REPORT w:6 $BACK w:4 $REPLAYS w:4 s:replays $REPLAY_ROW w:1 $REPLAY_PLAY w:10 s:playback \
	e:__zh.importer.listUserData().then(l=>l.some(f=>/\/replays\/.+\.rep$/.test(f.path))) N:Fatal"

# 5. Options: 800x600, brightness, music and scroll speed -> Accept -> Options.ini; the next start uses them.
run options "w:3 $OPTIONS w:4 c:640,278 w:2 c:580,300 w:2 c:748,244 w:1 c:748,113 w:1 c:47,401 w:2 c:702,566 w:5 c:89,189 w:4 \
	e:document.getElementById('canvas').width===800 \
	e:(async()=>{const~f=await~__zh.importer.readUserFile('command~and~conquer~generals~zero~hour~data/options.ini');const~t=await~f.text();return~/Resolution~=~800~600/.test(t)&&/Gamma~=~70/.test(t)&&/MusicVolume~=~80/.test(t)&&/ScrollFactor~=~79/.test(t)})() \
	reload u:document.getElementById('canvas').width===800&&__zh.state().launchSize[0]===800 s:after-reload"

# 6. The window: the picture stays in the window with bars, fullscreen gives the game the keys, a hidden tab pauses the
#    game and the audio, the Screenshot button saves a picture, the edge of the screen scrolls, leaving the page stops it.
run window "$IN_GAME size:700,500 w:1 e:document.getElementById('canvas').style.height==='500px' size:1100,800 \
	fs w:2 e:__zh.state().fullscreen===true fs w:2 e:__zh.state().fullscreen===false \
	q:#screenshot w:6 e:__zh.importer.listUserData().then(l=>l.some(f=>/\/screenshots\/.+\.(jpg|png)$/.test(f.path))) \
	v:window.__zhFrames hide w:3 v:window.__zhFrames e:window.zhWebAudio.ctx.state==='suspended' show w:3 e:window.zhWebAudio.ctx.state==='running' N:RuntimeError"

# 7. The game's own cursors (the starter pack has none: make_ani.py adds animated ones to the built pack first).
pack="$site/starterpack"
[ -d "$pack/data/ini" ] && python3 "$here/make_ani.py" "$pack" > /dev/null
run cursors "w:4 m:300,300 u:window.zhCursor.current().loaded.length>0 \
	e:window.zhCursor.current().loaded.length>0&&window.zhCursor.current().loaded.every(c=>c[1]===4) \
	e:/3~5,~auto/.test(window.zhCursor.current().css) \
	e:(async()=>{const~seen=new~Set();for(let~i=0;i<20;i++){seen.add(document.getElementById('canvas').style.cursor);await~new~Promise(r=>setTimeout(r,60))}return~seen.size>=3})()"

# 7b. Things that go wrong in the engine (-webcrashtest makes it fail on purpose after 60 frames): the page says what, offers
#     to play again, and never hangs.
for kind in oom badalloc ini trap abort; do
	trap_flag=""
	case $kind in
		oom|badalloc) title="ran~out~of~memory" ;;
		ini) title="could~not~be~read" ;;
		trap|abort) title="stopped~unexpectedly"; trap_flag="--expect-trap" ;;
	esac
	run crash-$kind "u:__zh.state().gameEnded===true e:document.getElementById('ended').hidden===false \
		e:/$title/.test(document.getElementById('ended-title').textContent) e:document.getElementById('errors').hidden===false \
		e:document.getElementById('play-again').hidden===false" --arg -webcrashtest=$kind $trap_flag
done

# 8. Retina: every canvas pixel is a screen pixel.
run sharp "w:5 e:document.getElementById('canvas').width===1800&&document.getElementById('canvas').style.width==='900px'" \
	--dpr 2 --resolution fitsharp --size 900x600

echo "$failed flow(s) failed"
exit $((failed > 0))
