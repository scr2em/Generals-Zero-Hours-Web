#!/usr/bin/env bash
# Gameplay tests on the real Zero Hour data, run on the player's own computer (the game data never leaves it).
# Gameplay (AI, player assists, balance) is never judged on the starter pack; these tests are how it is judged.
#
#   ZH_PATH="/path/to/Zero Hour" [GENERALS_PATH="/path/to/Generals"] scripts/gameplay/realdata_tests.sh [options] [suite...]
#
# Suites (default: all, in this order):
#   boot     start the game like a player (intro videos clicked away, main menu and its shell map) and watch for a crash
#   1v1      Expert against Hard, same faction, for America, China and GLA (map $MAP_1V1)
#   team     2v2 on $MAP_TEAM: two Experts against two Hard AIs from both sides of the map, and an Expert next to a weak
#            ally (an Easy AI in place of a human) against two Experts: do the Expert allies attack?
#   allyhelp the user's team game on $MAP_TEAM (starts $ALLY_STARTS, 50,000 cash): a human stand-in (an idle player: a computer
#            player without its AI, who builds and orders nothing; or an Easy AI) and two Expert allies against two Experts. Do the
#            Expert allies come to help when the stand-in's base is attacked, and go back afterwards? Once with the help switched off
#            (off-allyhelp) for comparison
#   bunker   China Expert against Hard: does it put infantry in its bunkers?
#   assists  the player assists, scripted (scripts/assistbench/scenarios/realdata: formations, protect links, stances, idle hotkeys, repeat production), seconds each
#
# What a pass looks like in the team suite: every Expert launches its first wave ("WAVE launches") within about 11 game minutes
# (the table says "min 11" or less) and not every Expert sits under alarm for more than a quarter of the game (seconds under alarm
# below about 400 in 30 minutes; an alarm that lasts longer than 90 s ends "(stale"); in the trace the line "waves: army A of N
# needed (...; allies X)" shows allies above 0 once the allies have an army. Compare with the old behaviour by adding the words
# "off-team+bdcap" to the variant of an Expert (expert:China:trace+off-team+bdcap@1).
#
# What a pass looks like in the allyhelp suite (the "ally help" column: per Expert, alarms about an allied base / teams sent /
# seconds helping, and when the stand-in fell): when the stand-in's base is attacked, the trace of an Expert ally says
# "ALLYHELP alarm N: player P base ... response on" and, unless its own base has an alarm at the time or the stand-in's defenders
# are enough, "ALLYHELP: N team(s) to player P base at (x,y) against V (own home H) ..." within a few seconds; every help ends with
# "ALLYHELP clear after N s: K team(s) go back" (no help that lasts the whole game; seconds helping below about a quarter of the
# game), the Experts still launch waves ("WAVE launches", first by minute 11), and the stand-in holds out longer than in the
# off-allyhelp match of the same seed (it fell later, or not at all). In the off-allyhelp match the alarm lines say "response OFF"
# and no team is sent.
#
# What a pass looks like in the assists suite: every scenario passes (assists/report.md). For split fire on units that answer
# an attack by themselves, stance-split-retaliate (tournamenta, human:America against idle:China; eight Rangers with split
# fire, a Red Guard shoots at them, a second one 20 frames later): assists/stance-split-retaliate.trace.txt has between one and
# eight "ASSIST stance split: unit N leaves A (enough on its way) for B" (A the first Red Guard, never back to it), both Red
# Guards dead within 300 frames and every Ranger alive. For the column, formation-column (tournamenta, three Crusaders, four
# Rangers, three Missile Defenders in rows of two): every unit within 30 of its slot (assists/formation-column.trace.txt,
# "expect shape army column" with maxError below 30), the Crusaders ahead of the Missile Defenders, and any unit pushed off
# its slot by a late one sent back ("ASSIST formation return id=N ... (trip 1)" or "(trip 2)", never "(trip 3)").
# See docs/PLAYER_ASSISTS.md for the other scenarios.
#
# What a pass looks like in the 1v1 suite: on the symmetrical 2-player map every Expert wins at least as often as the Hard AI
# of its faction and ends the timeouts with more value (final.value in the match JSON). The trace of the Expert
# (1v1/matches/*.trace.txt) shows its features at work:
#   * surplus production: "SURPLUS: <unit> ordered at <factory>" lines from about 150 s on, the status line
#     "surplus production: N units ordered (S spent), A given to teams" grows, and the Expert ends the game with little
#     money left (money.final in the match JSON a few thousand at most, where Hard keeps tens of thousands).
#   * razing and hunting: a wave that stands in an enemy base attacks its structures ("razing: N attack orders on
#     structures" grows); with no target known and a far stronger army, "team T HUNTS" lines. No team of the per-team
#     lines ("  team T mode M ... target ... units N idle I") stands idle for minutes with a target it never reaches.
#   * reinforcements: "team T: the force that went out is gone, N reinforcement(s) are the team now" can appear; such a
#     team joins the next wave or follow-up group (it does not stay in the base until the end).
#   * enemy supply near ours: "CONTEST: enemy <supply center> at (x,y), D from our supply center: N team(s) ... attack it"
#     when the enemy builds a supply center next to one of the Expert's.
#   * expansion: "EXPAND: supply center at the supply source at (x,y)" at least once in the first 10 minutes.
#   * base defence: damage by an enemy nobody sees gives "BASEDEF: only unseen damage at (x,y) for 20 s: one team looks,
#     N team(s) go back to their role" (not the whole army at home); a recall during a wave says "base threatened ...:
#     N team(s) recalled, M team(s) in a fight stay in it".
#
# Options:
#   --quick           fewer games (about a third of the time)
#   --workers N       games at the same time (default 2; each needs about 1 GB of memory and a core)
#   --build-dir DIR   web build to test (default build/web, built first by scripts/web/run.sh --build-only)
#   --native-dir DIR  native headless build (default build/native-headless, from `cmake --preset native-headless`).
#                     When it exists, the matches are played with it (faster, no browser); boot still uses the web build
#   --web             play the matches in the browser even when the native build exists
#   --no-build        test the build as it is
#   -h, --help        this text
#
# Environment: ZH_PATH (required), GENERALS_PATH (optional), MAP_1V1 (default "tournament desert": two players,
# the same income at both starts), MAP_TEAM (default
# "hostile dawn"), TEAM_STARTS (default 1,2,4,6: the first two are one team's side of the map, the last two the other's), ALLY_STARTS (default 1,2,3,4,6:
# the stand-in and its two Expert allies on one side, two Experts on the other, as in the user's game).
#
# The report goes to test-reports/<date>-<commit>/ (summary.md first). Commit and push that folder so it can be read:
#   git add test-reports && git commit -m "Gameplay test report" && git push
set -euo pipefail

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

QUICK=0
WORKERS=2
BUILD_DIR=build/web
NATIVE_DIR=build/native-headless
USE_WEB=0
BUILD=1
SUITES=()
while [ $# -gt 0 ]; do
	case "$1" in
		--quick) QUICK=1; shift ;;
		--workers) WORKERS="$2"; shift 2 ;;
		--build-dir) BUILD_DIR="$2"; shift 2 ;;
		--native-dir) NATIVE_DIR="$2"; shift 2 ;;
		--web) USE_WEB=1; shift ;;
		--no-build) BUILD=0; shift ;;
		-h|--help) usage; exit 0 ;;
		boot|1v1|team|allyhelp|bunker|assists) SUITES+=("$1"); shift ;;
		*) echo "Unknown option or suite: $1" >&2; usage >&2; exit 2 ;;
	esac
done
[ ${#SUITES[@]} -gt 0 ] || SUITES=(boot 1v1 team allyhelp bunker assists)

if [ -z "${ZH_PATH:-}" ] || [ ! -d "$ZH_PATH" ]; then
	echo "Set ZH_PATH to your Zero Hour folder (the one with INIZH.big), e.g." >&2
	echo "  ZH_PATH=\"\$HOME/Games/Command and Conquer Generals Zero Hour\" $0" >&2
	exit 2
fi
if [ -n "${GENERALS_PATH:-}" ] && [ ! -d "$GENERALS_PATH" ]; then
	echo "GENERALS_PATH does not exist: $GENERALS_PATH" >&2
	exit 2
fi

MAP_1V1="${MAP_1V1:-tournament desert}"
MAP_TEAM="${MAP_TEAM:-hostile dawn}"
TEAM_STARTS="${TEAM_STARTS:-1,2,4,6}"
ALLY_STARTS="${ALLY_STARTS:-1,2,3,4,6}"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

# 1. Build ------------------------------------------------------------------------------------------------------------
# The matches use the native headless build when it has been configured (cmake --preset native-headless), else the web
# build in the browser. Starting the game like a player (boot) and the scripted assists always need the web build.
NATIVE=0
if [ "$USE_WEB" = 0 ] && [ -f "$NATIVE_DIR/CMakeCache.txt" ]; then
	NATIVE=1
	if [ "$BUILD" = 1 ]; then
		cmake --build "$NATIVE_DIR" --target zh_headless
	fi
	[ -x "$NATIVE_DIR/GeneralsMD/zh_headless" ] || { echo "No native build in $NATIVE_DIR (run without --no-build, or use --web)" >&2; exit 1; }
fi
NEED_WEB=$((1 - NATIVE))
for suite in "${SUITES[@]}"; do case "$suite" in boot|assists) NEED_WEB=1 ;; esac; done
SITE="$BUILD_DIR/GeneralsMD"
if [ "$NEED_WEB" = 1 ]; then
	if [ "$BUILD" = 1 ]; then
		scripts/web/run.sh --build-only --build-dir "$BUILD_DIR"
	fi
	[ -f "$SITE/z_generals.html" ] || { echo "No build in $SITE (run without --no-build)" >&2; exit 1; }
fi

# 2. Playwright (drives Chrome; the installed Google Chrome is used when there is one) --------------------------------
command -v node >/dev/null 2>&1 || { echo "Node.js 20 or newer is needed (brew install node)" >&2; exit 1; }
if [ "$NEED_WEB" = 1 ]; then
	NODE_DIR="$HOME/.cache/zh-gameplay-node"
	export NODE_PATH="$NODE_DIR/node_modules${NODE_PATH:+:$NODE_PATH}"
	if ! node -e "require('playwright')" >/dev/null 2>&1; then
		echo "Installing Playwright into $NODE_DIR (once) ..."
		mkdir -p "$NODE_DIR"
		npm install --silent --prefix "$NODE_DIR" playwright >/dev/null
	fi
	if [ ! -x "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" ] && [ -z "${CHROMIUM_PATH:-}" ]; then
		(cd "$NODE_DIR" && npx --yes playwright install chromium >/dev/null)
	fi
fi

# 3. Run the suites ---------------------------------------------------------------------------------------------------
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
OUT="test-reports/$(date +%Y%m%d-%H%M)-$COMMIT"
mkdir -p "$OUT"
PROFILE="$HOME/.cache/zh-gameplay-profile"   # keeps the copied game data between runs (with the fixed port)
PORT=8950
DATA=(--zh "$ZH_PATH")
[ -n "${GENERALS_PATH:-}" ] && DATA+=(--generals "$GENERALS_PATH")
BENCH_WEB=(node scripts/aibench/aibench.mjs --site "$SITE" "${DATA[@]}" --profile "$PROFILE" --port "$PORT" --workers "$WORKERS")
if [ "$NATIVE" = 1 ]; then
	BENCH=(node scripts/aibench/aibench.mjs --native "$NATIVE_DIR/GeneralsMD/zh_headless" "${DATA[@]}" --workers "$WORKERS")
else
	BENCH=("${BENCH_WEB[@]}")
fi
if [ "$QUICK" = 1 ]; then SEEDS1=2; SEEDST=1; else SEEDS1=4; SEEDST=2; fi

{
	echo "# Gameplay tests on the real game data"
	echo
	echo "Commit $COMMIT ($(git log -1 --format=%s 2>/dev/null || true)), $(date '+%Y-%m-%d %H:%M'), $(uname -sm)."
	echo "Suites: ${SUITES[*]}; quick: $QUICK; maps: 1v1 \"$MAP_1V1\", team \"$MAP_TEAM\" (starts $TEAM_STARTS)."
	echo "Matches played with the $([ "$NATIVE" = 1 ] && echo "native headless build ($NATIVE_DIR)" || echo "web build in the browser ($SITE)")."
	echo
} > "$OUT/summary.md"

status=0
run_suite() {   # name, then bench arguments
	local name="$1"; shift
	local bench=("${BENCH[@]}")
	[ "$name" = boot ] && bench=("${BENCH_WEB[@]}")
	echo
	echo "==> $name"
	"${bench[@]}" --out "$OUT/$name" "$@" 2>&1 | tee "$OUT/$name.txt" || status=1
}

for suite in "${SUITES[@]}"; do
	case "$suite" in
		boot)
			run_suite boot --boot 120 ;;
		1v1)
			run_suite 1v1 --map "$MAP_1V1" --timeout 30 --match-timeout 2400 --seeds "$SEEDS1" --determinism 1 --keep-logs \
				--matchup "expert:America:trace,hard:America" --matchup "expert:China:trace,hard:China" \
				--matchup "expert:GLA:trace,hard:GLA" ;;
		team)
			run_suite team --map "$MAP_TEAM" --timeout 30 --match-timeout 3000 --seeds "$SEEDST" --determinism 0 --keep-logs \
				--no-rotate --starts "$TEAM_STARTS" \
				--matchup "expert:China:trace@1,expert:America:trace@1,hard:GLA@2,hard:China@2" \
				--matchup "hard:GLA@1,hard:China@1,expert:China:trace@2,expert:America:trace@2" \
				--matchup "easy:China@1,expert:America:trace@1,expert:GLA@2,expert:China@2" ;;
		allyhelp)
			# the user's game: the human (China) and two Expert allies against two Experts, 50,000 cash; the human is an idle player
			# (m1, and m2 with the help off), or an Easy AI that builds a base (m3)
			run_suite allyhelp --map "$MAP_TEAM" --timeout 27 --match-timeout 3000 --seeds "$SEEDST" --determinism 0 --keep-logs \
				--no-rotate --starts "$ALLY_STARTS" --engine-arg cash=50000 \
				--matchup "idle:China@1,expert:China:trace@1,expert:America:trace@1,expert:GLA:trace@2,expert:China:trace@2" \
				--matchup "idle:China@1,expert:China:trace+off-allyhelp@1,expert:America:trace+off-allyhelp@1,expert:GLA:trace@2,expert:China:trace@2" \
				--matchup "easy:China@1,expert:China:trace@1,expert:America:trace@1,expert:GLA:trace@2,expert:China:trace@2" ;;
		bunker)
			run_suite bunker --map "$MAP_1V1" --timeout 20 --match-timeout 2000 --seeds 2 --determinism 0 --keep-logs \
				--matchup "expert:China:trace,hard:America" ;;
		assists)
			# scripted player assist scenarios (-assistMatch, no rendering): each names its map and units, see scripts/assistbench
			echo
			echo "==> assists"
			node scripts/assistbench/assistbench.mjs --site "$SITE" "${DATA[@]}" --profile "$PROFILE" --port "$PORT" --workers "$WORKERS" \
				--out "$OUT/assists" scripts/assistbench/scenarios/realdata 2>&1 | tee "$OUT/assists.txt" || status=1 ;;
	esac
done

# 4. Summary: results and what the Expert AIs did (from their trace), small enough to commit --------------------------
python3 - "$OUT" <<'PY' >> "$OUT/summary.md"
import glob, json, os, re, sys
out = sys.argv[1]
def section(title): print(f"\n## {title}\n")
for suite in ("boot", "1v1", "team", "allyhelp", "bunker", "assists"):
    d = os.path.join(out, suite)
    if not os.path.isdir(d): continue
    section(suite)
    txt = os.path.join(out, suite + ".txt")
    if suite == "assists":
        # scripted scenarios: the bench's own report (pass/fail per scenario, the first failing check); the steps, the checks
        # and the decisions of the assists are in assists/<scenario>.trace.txt
        rep = os.path.join(d, "report.md")
        if os.path.exists(rep):
            body = open(rep, errors="replace").read().split("\n", 1)[-1]
            print(body.strip()[:8000])
        else:
            lines = open(txt, errors="replace").read().splitlines() if os.path.exists(txt) else []
            print("(no report)\n\n```\n" + "\n".join(lines[-20:]) + "\n```")
        continue
    if suite == "boot":
        lines = open(txt, errors="replace").read().splitlines() if os.path.exists(txt) else []
        print("\n".join(l for l in lines if l.startswith("boot:")) or "(no result)")
        bl = os.path.join(d, "boot.log")
        if os.path.exists(bl):
            tail = open(bl, errors="replace").read().splitlines()[-40:]
            print("\nLast lines of the game's log:\n\n```\n" + "\n".join(tail) + "\n```")
        continue
    rep = os.path.join(d, "report.md")
    if os.path.exists(rep):
        body = open(rep, errors="replace").read().split("\n", 1)[-1]   # without its title
        print(re.sub(r"(?m)^(#+) ", lambda m: "#" + m.group(1) + " ", body).strip()[:8000])
    print("\n| match | result | per Expert (player): waves launched (first at minute), base alarms, seconds under alarm (stale ones), bunker entries | ally help per Expert (player): alarms about an allied base, teams sent, seconds helping; players that fell (minute) | errors |\n|---|---|---|---|---|")
    for f in sorted(glob.glob(os.path.join(d, "matches", "*.json"))):
        if f.endswith("-replay.json"): continue
        j = json.load(open(f))
        mid = os.path.basename(f)[:-5]
        res = j.get("result") or {}
        r = res.get("result") or {}
        players = (j.get("job") or {}).get("players") or []
        winners = [players[i]["label"] + (f"@{players[i].get('team')}" if players[i].get("team") is not None else "") for i in r.get("winners", []) if i < len(players)]
        outcome = (r.get("outcome") or "error") + (f" ({', '.join(winners)})" if winners else "") + (f", {r.get('frames', 0) // 1800} min" if r.get("frames") else "")
        log = os.path.join(d, "matches", mid + ".log")
        per = {}
        errors = []
        if os.path.exists(log):
            for line in open(log, errors="replace"):
                m = re.match(r"AISTRAT\[p(\d+) f(\d+)\] (.*)", line)
                if m:
                    p = per.setdefault(m.group(1), [0, 0, 0, 0, 0, 0, 0, 0, 0])
                    t = m.group(3)
                    if t.startswith("WAVE launches"):
                        p[0] += 1
                        if p[4] == 0: p[4] = int(m.group(2))     # frame of the first wave
                    elif t.startswith("BASEDEF alarm"): p[1] += 1
                    elif t.startswith("BASEDEF clear after"):
                        p[2] += int(re.match(r"BASEDEF clear after (\d+)", t).group(1))
                        if "(stale:" in t: p[5] += 1
                    elif t.startswith("BUNKER:") and " goes into " in t: p[3] += 1
                    elif t.startswith("ALLYHELP alarm"): p[6] += 1
                    elif t.startswith("ALLYHELP: ") and " team(s) to player " in t: p[7] += int(re.match(r"ALLYHELP: (\d+) team", t).group(1))
                    elif t.startswith("ALLYHELP clear after"):
                        h = re.search(r"helped (\d+) s", t)
                        if h: p[8] += int(h.group(1))
                elif re.search(r"RuntimeError|Engine thread stopped|Assertion failed|Fatal error", line):
                    errors.append(line.strip()[:120])
            # keep the decisions of the Expert AIs and drop the rest of the engine output (size)
            with open(os.path.join(d, "matches", mid + ".trace.txt"), "w") as t:
                t.writelines(l for l in open(log, errors="replace") if l.startswith("AISTRAT[") or "AIMATCH" in l)
            if j.get("ok"): os.remove(log)
        stats = "; ".join(f"p{k}: {v[0]} (min {v[4] // 1800 if v[0] else '-'}), {v[1]}, {v[2]} ({v[5]}), {v[3]}" for k, v in sorted(per.items())) or "-"
        helped = "; ".join(f"p{k}: {v[6]}, {v[7]}, {v[8]}" for k, v in sorted(per.items()) if v[6] or v[7]) or "-"
        fell = ", ".join(f"{p.get('difficulty')}:{p.get('side')} p{p.get('playerIndex', '?')} ({p['defeatedFrame'] // 1800})"
                         for p in (res.get("players") or []) if p.get("defeatedFrame"))
        print(f"| {mid} | {outcome} | {stats} | {helped}; fell: {fell or 'none'} | {(j.get('error') or '') + ' ' + ' / '.join(errors[:2])} |")
PY

echo
echo "Report: $OUT/summary.md"
echo "Send it:  git add test-reports && git commit -m \"Gameplay test report $COMMIT\" && git push"
exit $status
