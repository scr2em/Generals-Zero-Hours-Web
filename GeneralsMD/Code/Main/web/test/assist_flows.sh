#!/bin/sh
# The player assist flows of the web build, driven through the real game with the free starter content in headless
# Chromium (software WebGL: a match runs at a few frames a second on a busy machine, so allow an hour for all of them).
#
#   source emsdk_env.sh
#   cmake --preset emscripten -B build/web -DRTS_WEB_FFMPEG=OFF -DRTS_WEB_PYODIDE=OFF && ninja -C build/web z_generals starter_pack
#   PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers NODE_PATH=/opt/node22/lib/node_modules \
#       GeneralsMD/Code/Main/web/test/assist_flows.sh build/web/GeneralsMD [out dir]
#
# FLOWS="formations options" runs only those.  Each flow is one starter_flow.mjs run (see its header for the steps).
# The matches start with extra units (-assistTest, see GameLogic/AssistTest.cpp) and print what the assists decide
# (-assistDebug): the flows wait for and check those "ASSIST ..." log lines; the screenshots are for people.
#
# Flows:
#   options          the Player assists dialog of the Options screen switches the formation assist on and writes Options.ini
#   formations       picker, hotkeys, hotkey group memory, move in formation, drag to aim
#   formations-off   without the option nothing happens (no panel, hotkeys do nothing)
#   formations-rule  option on, but the match does not allow assists (the setup check box): hotkeys do nothing
#   formations-replay  a match with formation orders is recorded, then played back: no CRC mismatch, same orders
site=$(cd "$1" && pwd)
out=${2:-${TMPDIR:-/tmp}/zh-assist-flows}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
failed=0
flow="node $here/starter_flow.mjs --site $site --port ${PORT:-8971}"

# main menu and setup screens (800x600 design coordinates, see shell_flows.sh)
SKIRMISH="c:185,221"; OPTIONS="c:185,413"; START="c:699,566"; BACK="c:103,566"; REPLAYS="c:185,366"
MENU="c:515,449"; PAUSE_EXIT="c:399,412"; YES="c:318,368"; OK_REPORT="c:699,566"
REPLAY_ROW="c:62,100"; REPLAY_PLAY="c:124,502"
ASSIST_CHECKBOX="c:565,480"		# "Player assists allowed" on the skirmish setup screen
ASSIST_BUTTON="c:208,481"			# "Player assists..." on the Options screen
OPT_FORMATIONS="c:380,190"; OPT_CLOSE="c:399,270"		# in the Player assists dialog

# the units the matches start with: left of the base, in the first screen
UNITS="0:IronwoodRifleman:6+0:IronwoodTank:3+0:IronwoodRocketeer:4+0:IronwoodScout:2"
GAME="w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~2~x~IronwoodScout w:3"
SELECT="k:Home w:5 r:30,312,430,436 w:4"			# the camera on the base, a box over the new units (not at the screen edge: it scrolls)
WEDGE="c:152,415"; LINE="c:62,415"
CTRLALT="kd:Control kd:Alt w:1"; ENDCTRLALT="w:1 ku:Alt ku:Control w:2"

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
assist="--arg -assistTest --arg $UNITS --arg -assistDebug"

# 1. The dialog of the Options screen: formations on; Options.ini has it at once.
run options "w:3 $OPTIONS w:6 $ASSIST_BUTTON w:4 s:dialog $OPT_FORMATIONS w:3 s:toggled \
	e:(async()=>{const~f=await~__zh.importer.readUserFile('command~and~conquer~generals~zero~hour~data/options.ini');const~t=await~f.text();return~/AssistFormations~=~1/.test(t)})() \
	$OPT_CLOSE w:3 s:closed $OPT_FORMATIONS w:2 N:RuntimeError" --arg -assistDebug

# 2. Formations.  The picker sets Wedge, a hotkey sets Box, the hotkey group keeps it, a click moves in formation, a drag aims it.
run formations "$GAME W:ASSIST~match~allowed=1 $SELECT s:selected \
	$WEDGE W:ASSIST~formation~set~type=3~units=15 s:wedge \
	$CTRLALT k:Digit4 $ENDCTRLALT W:ASSIST~formation~set~type=4~units=15 \
	kd:Control w:1 k:Digit1 w:1 ku:Control w:3 \
	R:600,200 w:3 k:Digit1 w:4 s:group-reselected \
	c:200,230 W:ASSIST~formation~move~type=4~units=15~angle=[-0-9.]+~width=0 w:3 \
	m:250,200 w:1 bd:right w:1 m:300,195 m:380,185 w:4 s:aim bu:right W:ASSIST~formation~move~type=4~units=15~angle=[-0-9.]+~width=[1-9] w:20 s:aimed \
	N:RuntimeError N:ASSISTTEST~cannot" $assist --options AssistFormations=1

# 3. Without the option: no panel, hotkeys do nothing.
run formations-off "$GAME W:ASSIST~match~allowed=0 $SELECT s:selected \
	$CTRLALT k:Digit3 $ENDCTRLALT s:nothing N:ASSIST~formation~set N:RuntimeError" $assist

# 4. The option is on but the match does not allow assists (the check box of the setup screen is cleared).
run formations-rule "w:3 $SKIRMISH w:3 $ASSIST_CHECKBOX w:2 s:setup $START W:ASSISTTEST~created~2~x~IronwoodScout w:3 W:ASSIST~match~allowed=0 $SELECT s:selected \
	$CTRLALT k:Digit3 $ENDCTRLALT s:nothing N:ASSIST~formation~set N:RuntimeError" $assist --options AssistFormations=1

# 5. Record formation orders, play the replay back: the logic CRCs of the replay all match and the orders are carried out again.
run formations-replay "$GAME W:ASSIST~match~allowed=1 $SELECT \
	$WEDGE W:ASSIST~formation~set~type=3~units=15 c:200,230 W:ASSIST~formation~move~type=3~units=15 f:500 \
	$MENU w:4 $PAUSE_EXIT w:4 $YES w:10 $OK_REPORT w:6 $BACK w:5 $REPLAYS w:5 $REPLAY_ROW w:1 $REPLAY_PLAY W:replay=1 f:450 \
	K:2:ASSIST~formation~set~type=3 K:2:ASSIST~formation~move~type=3~units=15 N:CRC~Mismatch N:RuntimeError" $assist --options AssistFormations=1

echo "$failed flow(s) failed"
exit $((failed > 0))
