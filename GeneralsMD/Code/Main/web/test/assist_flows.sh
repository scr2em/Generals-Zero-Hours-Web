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
#   protect          riflemen protect the headquarters, an enemy attacks it, they answer, return home; a move by hand moves the home
#   protect-replay   the same, recorded and played back
#   base-alert       the "base under attack" response: idle army units are sent to the attacked place and back (Alt+A twice)
#   base-alert-replay  the same, recorded and played back
#   stance-kite      stance: rocketeers step back while their weapon reloads
#   stance-retreat   stance: riflemen hurt by the test hook go to an infirmary and come back healed (bounded waits, minutes)
#   stance-spread    stance: riflemen keep apart when an enemy mortar (big blast) is about
#   stance-split     stance: riflemen do not pile on a target that has enough fire on it
#   stance-replay    all four stances at once, recorded and played back
#   odds             the odds meter: own riflemen selected, the mouse over an enemy rifleman gives a favourable verdict
#   coverage         the defence coverage view: rings of a ground and an air defence, uncovered stretches of the base edge
#   repeat-replay    repeat production on the headquarters (W, Alt+Q): the worker is built again; recorded and played back
#   repeat-save      the same, saved and loaded from the pause menu: the repeat state is in the save and goes on
#   info-strip       the info strip: income, army value (two tanks), game time and the actions per minute of a few clicks
#   view-options     the dialog's "+" of the zoom-out and the panel size rows: Options.ini has them; in a match the mouse
#                    wheel zooms out beyond the game's limit
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
OPT_FORMATIONS="c:400,67"; OPT_CLOSE="c:399,475"		# in the Player assists dialog
OPT_ZOOM_UP="c:607,367"; OPT_UISCALE_UP="c:607,397"		# the "+" buttons of the zoom-out and panel size rows

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

# 6. Protect: three riflemen are told to protect the headquarters; an enemy scout shoots at it; they attack-move there and
#    come back; moving them by hand sets their new home; the stop command removes the link.
PROTECT_UNITS="0:IronwoodRifleman:3+1:IronwoodRifleman:1:0:170:-60"
PROTECT_GAME="w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~1~x~IronwoodRifleman~for~player~1 w:3"
PROTECT_DO="k:Home w:5 c:400,215 w:3 s:hq kd:Control w:1 k:Digit2 w:1 ku:Control w:2 r:120,345,260,400 w:4 s:selected kd:Alt w:1 k:KeyP w:1 ku:Alt w:3 s:picking k:Digit2 W:ASSIST~protect~link~[0-9]+~protects~0~objects~and~hotkey~group~2 s:linked"
run protect "$PROTECT_GAME W:ASSIST~match~allowed=1 $PROTECT_DO \
	W:ASSIST~protect~[0-9]+~answers~the~alarm s:answering W:ASSIST~protect~[0-9]+~goes~home W:ASSIST~protect~[0-9]+~is~home s:home \
	c:200,230 W:ASSIST~protect~home~of~[0-9]+~moved~by~hand w:3 \
	kd:Alt w:1 k:KeyU w:1 ku:Alt w:3 W:ASSIST~protect~link~[0-9]+~removed N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$PROTECT_UNITS" --arg -assistDebug --options AssistProtect=1

run protect-replay "$PROTECT_GAME W:ASSIST~match~allowed=1 $PROTECT_DO W:ASSIST~protect~[0-9]+~answers~the~alarm W:ASSIST~protect~[0-9]+~is~home f:700 \
	$MENU w:4 $PAUSE_EXIT w:4 $YES w:10 $OK_REPORT w:6 $BACK w:5 $REPLAYS w:5 $REPLAY_ROW w:1 $REPLAY_PLAY W:replay=1 f:600 \
	K:2:ASSIST~protect~link~[0-9]+~protects N:CRC~Mismatch N:RuntimeError" --arg -assistTest --arg "$PROTECT_UNITS" --arg -assistDebug --options AssistProtect=1

# 7. Defence coverage: a guard tower (ground) east and a flak tower (air only) west of the base; the hotkey switches the view on
#    and it finds stretches of the base edge that only one of them covers.
COVER_UNITS="0:IronwoodGuardTower:1:0:220:-90+0:IronwoodFlakTower:1:0:-220:-60"
run coverage "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~1~x~IronwoodFlakTower w:3 k:Home w:5 s:before \
	kd:Alt w:1 k:KeyD w:1 ku:Alt w:4 W:ASSIST~coverage~ground~defences=1~air~defences=1~bases=1~uncovered~ground~steps=[1-9][0-9]*~air~steps=[1-9][0-9]* s:coverage \
	N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$COVER_UNITS" --arg -assistDebug --options AssistCoverageView=1

# 8. Base under attack: three idle riflemen stand away from the base, an enemy rifleman shoots the headquarters; the alert raises the
#    response, the hotkey sends the idle units and the second press sends them back.
ALERT_UNITS="0:IronwoodRifleman:3+1:IronwoodRifleman:1:0:170:-60"
ALERT_GAME="w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~1~x~IronwoodRifleman~for~player~1 w:3"
ALERT_DO="W:ASSIST~match~allowed=1 k:Home w:3 W:ASSIST~alert~raised~at s:alert kd:Alt w:1 k:KeyA w:1 ku:Alt W:ASSIST~base~defend:~[1-9]~units~sent~to s:defending \
	w:10 kd:Alt w:1 k:KeyA w:1 ku:Alt W:ASSIST~base~defend:~[1-9]~units~sent~back s:back"
run base-alert "$ALERT_GAME $ALERT_DO N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$ALERT_UNITS" --arg -assistDebug --options AssistBaseAlert=1

run base-alert-replay "$ALERT_GAME $ALERT_DO f:300 \
	$MENU w:4 $PAUSE_EXIT w:4 $YES w:10 $OK_REPORT w:6 $BACK w:5 $REPLAYS w:5 $REPLAY_ROW w:1 $REPLAY_PLAY W:replay=1 f:600 \
	K:2:ASSIST~base~defend:~[1-9]~units~sent~to N:CRC~Mismatch N:RuntimeError" --arg -assistTest --arg "$ALERT_UNITS" --arg -assistDebug --options AssistBaseAlert=1

# 9. Odds meter: three own riflemen are selected, the mouse is moved onto the lone enemy rifleman: the verdict is favourable.
run odds "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~1~x~IronwoodRifleman~for~player~1 w:3 k:Home w:5 r:120,345,260,400 w:4 \
	kd:Alt w:1 k:KeyO w:1 ku:Alt w:2 W:ASSIST~odds~view~on s:viewon m:700,270 w:2 m:699,268 w:3 W:ASSIST~odds~probe~armed s:hover W:ASSIST~odds~kind=1~ratio=[0-9.]+~own=[1-3]~enemy=1 s:odds \
	N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$ALERT_UNITS" --arg -assistDebug --options AssistOddsMeter=1

# 11. Unit stances.  The enemy appears at a late logic frame (the last field of the "-assistTest" entry), so the stance is set before
#     the fight starts.  The selection is a box over the units below the base.
STANCE_ASSIST_ON="W:ASSIST~match~allowed=1"
KITE_UNITS="0:IronwoodRocketeer:2:0:-150:-130+1:IronwoodRifleman:2:0:10:-130:900"
run stance-kite "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~2~x~IronwoodRocketeer~for~player~0 $STANCE_ASSIST_ON k:Home w:4 r:60,340,270,410 w:3 \
	kd:Alt w:1 k:KeyK w:1 ku:Alt w:2 W:ASSIST~stance~set~mask=1~value=1~percent=0~for~2~units s:kite_set \
	W:ASSISTTEST~created~2~x~IronwoodRifleman~for~player~1 W:ASSIST~stance~kite:~unit~[0-9]+~steps~back~from s:kite_step \
	N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$KITE_UNITS" --arg -assistDebug --options AssistStances=1

# The riflemen are hurt by the test hook (a "damage" entry of -assistTest: 60% off in logic frame 90, before or after the
# stance is set) instead of a fight, which may not come to them; every wait is bounded, so a failure costs minutes, not hours.
RETREAT_UNITS="0:IronwoodRifleman:2:0:-150:-130+0:IronwoodInfirmary:1:0:-230:-80+damage:0:IronwoodRifleman:60:90"
run stance-retreat "w:3 $SKIRMISH w:3 $START Wt:300:ASSISTTEST~created~1~x~IronwoodInfirmary~for~player~0 Wt:120:ASSIST~match~allowed=1 k:Home w:4 r:60,340,270,410 w:3 \
	kd:Alt w:1 k:KeyR w:1 ku:Alt w:2 kd:Alt w:1 k:KeyR w:1 ku:Alt w:2 kd:Alt w:1 k:KeyR w:1 ku:Alt w:2 Wt:120:ASSIST~stance~set~mask=2~value=2~percent=70~for~2~units s:retreat_set \
	Wt:240:ASSISTTEST~damaged~2~x~IronwoodRifleman \
	Wt:180:ASSIST~stance~retreat:~unit~[0-9]+~at~40%~goes~to~heal~facility s:retreating \
	Wt:300:ASSIST~stance~retreat:~unit~[0-9]+~goes~back~to~-?[0-9]+,-?[0-9]+~at~100% s:healed \
	N:RuntimeError N:ASSISTTEST~cannot N:ASSIST~stance~retreat:.*(rally~point|cannot)" --arg -assistTest --arg "$RETREAT_UNITS" --arg -assistDebug --options AssistStances=1

SPREAD_UNITS="0:IronwoodRifleman:3:0:-150:-130+0:IronwoodRifleman:3:0:-135:-130+1:IronwoodMortar:1:0:-60:-220:900"
run stance-spread "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~3~x~IronwoodRifleman~for~player~0 $STANCE_ASSIST_ON k:Home w:4 r:60,340,300,410 w:3 \
	kd:Alt w:1 k:KeyS w:1 ku:Alt w:2 W:ASSIST~stance~set~mask=4~value=4 s:spread_set \
	W:ASSISTTEST~created~1~x~IronwoodMortar~for~player~1 W:ASSIST~stance~spread:~(idle~)?unit~[0-9]+~(moves~apart|steps~aside) s:spread_step \
	N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$SPREAD_UNITS" --arg -assistDebug --options AssistStances=1

SPLIT_UNITS="0:IronwoodRifleman:8:0:-150:-130+1:IronwoodRifleman:2:0:-80:-170:900"
run stance-split "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~8~x~IronwoodRifleman~for~player~0 $STANCE_ASSIST_ON k:Home w:4 r:60,340,560,410 w:3 \
	kd:Alt w:1 k:KeyX w:1 ku:Alt w:2 W:ASSIST~stance~set~mask=8~value=8~percent=0~for~8~units s:split_set \
	Wt:300:ASSISTTEST~created~2~x~IronwoodRifleman~for~player~1 Wt:600:ASSIST~stance~split:~unit~[0-9]+~leaves~[0-9]+ s:split_switch \
	N:RuntimeError N:ASSISTTEST~cannot" --arg -assistTest --arg "$SPLIT_UNITS" --arg -assistDebug --options AssistStances=1

# all four at once, recorded and played back: same positions and no CRC mismatch
# (the riflemen are hurt to 25% in frame 300, below the 30% of one Alt+R: they go to the infirmary in the match and in the replay)
ALL_UNITS="0:IronwoodRifleman:4:0:-150:-130+0:IronwoodRocketeer:2:0:-150:-160+0:IronwoodInfirmary:1:0:-230:-80+1:IronwoodRifleman:3:0:-40:-230:900+1:IronwoodMortar:1:0:-60:-260:900+damage:0:IronwoodRifleman:75:300"
run stance-replay "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~4~x~IronwoodRifleman~for~player~0 $STANCE_ASSIST_ON k:Home w:4 r:60,320,400,410 w:3 \
	kd:Alt w:1 k:KeyK w:1 ku:Alt w:2 kd:Alt w:1 k:KeyR w:1 ku:Alt w:2 kd:Alt w:1 k:KeyS w:1 ku:Alt w:2 kd:Alt w:1 k:KeyX w:1 ku:Alt w:2 \
	W:ASSIST~stance~set~mask=8~value=8 W:ASSISTTEST~created~3~x~IronwoodRifleman~for~player~1 f:1500 \
	$MENU w:4 $PAUSE_EXIT w:4 $YES w:10 $OK_REPORT w:6 $BACK w:5 $REPLAYS w:5 $REPLAY_ROW w:1 $REPLAY_PLAY W:replay=1 f:600 \
	K:2:ASSIST~stance~set~mask=8 K:8:ASSIST~stance~retreat:~unit~[0-9]+~at~25%~goes~to~heal~facility N:CRC~Mismatch N:RuntimeError" --arg -assistTest --arg "$ALL_UNITS" --arg -assistDebug --options AssistStances=1

# 12. Repeat production: the headquarters is selected, W queues a worker, Alt+Q switches repeat on; the worker is built again.
#     Recorded and played back (no CRC mismatch, the same orders), and saved and loaded (the state is in the save).
REPEAT_ON="W:ASSIST~match~allowed=1 k:Home w:5 c:400,215 w:3 s:hq k:KeyW w:3 kd:Alt w:1 k:KeyQ w:1 ku:Alt \
	W:ASSIST~repeat~production:~factory~[0-9]+~on~.IronwoodWorker. s:repeat"
REPEATED="W:ASSIST~repeat~production:~factory~[0-9]+~queues~IronwoodWorker~again"
run repeat-replay "w:3 $SKIRMISH w:3 $START $REPEAT_ON $REPEATED f:900 \
	$MENU w:4 $PAUSE_EXIT w:4 $YES w:10 $OK_REPORT w:6 $BACK w:5 $REPLAYS w:5 $REPLAY_ROW w:1 $REPLAY_PLAY W:replay=1 f:900 \
	K:2:ASSIST~repeat~production:~factory~[0-9]+~queues~IronwoodWorker~again N:CRC~Mismatch N:RuntimeError" --arg -assistDebug --options AssistRepeatProduction=1

PAUSE_SAVELOAD="c:399,316"; NEW_SAVE_ROW="c:65,100"; SAVE_BUTTON="c:124,502"; OK_SAVE="c:302,350"
FIRST_SAVE_ROW="c:92,116"; LOAD_BUTTON="c:326,502"; CONFIRM_YES="c:312,330"
# (saved before the first worker is out: the worker that is built again after the load shows that the state was loaded)
run repeat-save "w:3 $SKIRMISH w:3 $START $REPEAT_ON N:queues~IronwoodWorker~again \
	$MENU w:4 $PAUSE_SAVELOAD w:4 $NEW_SAVE_ROW w:1 $SAVE_BUTTON w:3 $OK_SAVE w:4 s:saved \
	$PAUSE_SAVELOAD w:4 $FIRST_SAVE_ROW w:1 $LOAD_BUTTON w:3 $CONFIRM_YES W:ASSIST~loaded:~allowed=1,~repeat~production~on~1~buildings s:loaded \
	$REPEATED s:repeated N:could~not~be~loaded N:Error~loading~block N:RuntimeError" \
	--arg -assistDebug --options AssistRepeatProduction=1 --profile "$out/repeat-save/profile"

# 13. Camera zoom-out and panel size: the "+" buttons of the dialog (125% zoom-out, 110% panels) are in Options.ini; in a match
#     the wheel zooms out beyond the game's limit.
run view-options "w:3 $OPTIONS w:6 $ASSIST_BUTTON w:4 $OPT_ZOOM_UP w:2 $OPT_UISCALE_UP w:2 s:dialog \
	W:ASSIST~camera~zoom-out~allowance~125% W:ASSIST~panels~scale~110% \
	e:(async()=>{const~f=await~__zh.importer.readUserFile('command~and~conquer~generals~zero~hour~data/options.ini');const~t=await~f.text();return~/AssistMaxZoomPercent~=~125/.test(t)&&/AssistUIScalePercent~=~110/.test(t)})() \
	$OPT_CLOSE w:3 c:702,566 w:5 $SKIRMISH w:3 $START W:ASSIST~match~allowed k:Home w:5 m:400,300 \
	wheel:500 w:1 wheel:500 w:1 wheel:500 w:1 wheel:500 w:1 wheel:500 w:1 wheel:500 w:3 s:zoomed \
	W:ASSIST~camera~zoomed~out~to~1[0-9][0-9]%~of~the~game's~limit~.allowed~125%. N:RuntimeError" --arg -assistDebug

# 14. Info strip: two tanks for the army value, a few selections for the actions per minute; the strip prints its values.
run info-strip "w:3 $SKIRMISH w:3 $START W:ASSISTTEST~created~2~x~IronwoodTank w:3 k:Home w:4 c:400,215 w:2 r:30,312,430,436 w:2 c:400,215 w:2 \
	W:ASSIST~info~income=[0-9]+/min~army=[1-9][0-9]*~time=[0-9]+:[0-9][0-9]~apm=[1-9] s:strip N:RuntimeError N:ASSISTTEST~cannot" \
	--arg -assistTest --arg 0:IronwoodTank:2 --arg -assistDebug --options AssistInfoStrip=1

echo "$failed flow(s) failed"
exit $((failed > 0))
