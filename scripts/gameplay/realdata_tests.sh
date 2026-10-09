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
#   bunker   China Expert against Hard: does it put infantry in its bunkers?
#
# Options:
#   --quick           fewer games (about a third of the time)
#   --workers N       games at the same time (default 2; each needs about 1 GB of memory and a core)
#   --build-dir DIR   web build to test (default build/web, built first by scripts/web/run.sh --build-only)
#   --no-build        test the build as it is
#   -h, --help        this text
#
# Environment: ZH_PATH (required), GENERALS_PATH (optional), MAP_1V1 (default "tournamenta"), MAP_TEAM (default
# "hostile dawn"), TEAM_STARTS (default 1,2,4,6: the first two are one team's side of the map, the last two the other's).
#
# The report goes to test-reports/<date>-<commit>/ (summary.md first). Commit and push that folder so it can be read:
#   git add test-reports && git commit -m "Gameplay test report" && git push
set -euo pipefail

usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; }

QUICK=0
WORKERS=2
BUILD_DIR=build/web
BUILD=1
SUITES=()
while [ $# -gt 0 ]; do
	case "$1" in
		--quick) QUICK=1; shift ;;
		--workers) WORKERS="$2"; shift 2 ;;
		--build-dir) BUILD_DIR="$2"; shift 2 ;;
		--no-build) BUILD=0; shift ;;
		-h|--help) usage; exit 0 ;;
		boot|1v1|team|bunker) SUITES+=("$1"); shift ;;
		*) echo "Unknown option or suite: $1" >&2; usage >&2; exit 2 ;;
	esac
done
[ ${#SUITES[@]} -gt 0 ] || SUITES=(boot 1v1 team bunker)

if [ -z "${ZH_PATH:-}" ] || [ ! -d "$ZH_PATH" ]; then
	echo "Set ZH_PATH to your Zero Hour folder (the one with INIZH.big), e.g." >&2
	echo "  ZH_PATH=\"\$HOME/Games/Command and Conquer Generals Zero Hour\" $0" >&2
	exit 2
fi
if [ -n "${GENERALS_PATH:-}" ] && [ ! -d "$GENERALS_PATH" ]; then
	echo "GENERALS_PATH does not exist: $GENERALS_PATH" >&2
	exit 2
fi

MAP_1V1="${MAP_1V1:-tournamenta}"
MAP_TEAM="${MAP_TEAM:-hostile dawn}"
TEAM_STARTS="${TEAM_STARTS:-1,2,4,6}"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

# 1. Build ------------------------------------------------------------------------------------------------------------
if [ "$BUILD" = 1 ]; then
	scripts/web/run.sh --build-only --build-dir "$BUILD_DIR"
fi
SITE="$BUILD_DIR/GeneralsMD"
[ -f "$SITE/z_generals.html" ] || { echo "No build in $SITE (run without --no-build)" >&2; exit 1; }

# 2. Playwright (drives Chrome; the installed Google Chrome is used when there is one) --------------------------------
command -v node >/dev/null 2>&1 || { echo "Node.js 20 or newer is needed (brew install node)" >&2; exit 1; }
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

# 3. Run the suites ---------------------------------------------------------------------------------------------------
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
OUT="test-reports/$(date +%Y%m%d-%H%M)-$COMMIT"
mkdir -p "$OUT"
PROFILE="$HOME/.cache/zh-gameplay-profile"   # keeps the copied game data between runs (with the fixed port)
PORT=8950
DATA=(--zh "$ZH_PATH")
[ -n "${GENERALS_PATH:-}" ] && DATA+=(--generals "$GENERALS_PATH")
BENCH=(node scripts/aibench/aibench.mjs --site "$SITE" "${DATA[@]}" --profile "$PROFILE" --port "$PORT" --workers "$WORKERS")
if [ "$QUICK" = 1 ]; then SEEDS1=2; SEEDST=1; else SEEDS1=4; SEEDST=2; fi

{
	echo "# Gameplay tests on the real game data"
	echo
	echo "Commit $COMMIT ($(git log -1 --format=%s 2>/dev/null || true)), $(date '+%Y-%m-%d %H:%M'), $(uname -sm)."
	echo "Suites: ${SUITES[*]}; quick: $QUICK; maps: 1v1 \"$MAP_1V1\", team \"$MAP_TEAM\" (starts $TEAM_STARTS)."
	echo
} > "$OUT/summary.md"

status=0
run_suite() {   # name, then bench arguments
	local name="$1"; shift
	echo
	echo "==> $name"
	"${BENCH[@]}" --out "$OUT/$name" "$@" 2>&1 | tee "$OUT/$name.txt" || status=1
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
		bunker)
			run_suite bunker --map "$MAP_1V1" --timeout 20 --match-timeout 2000 --seeds 2 --determinism 0 --keep-logs \
				--matchup "expert:China:trace,hard:America" ;;
	esac
done

# 4. Summary: results and what the Expert AIs did (from their trace), small enough to commit --------------------------
python3 - "$OUT" <<'PY' >> "$OUT/summary.md"
import glob, json, os, re, sys
out = sys.argv[1]
def section(title): print(f"\n## {title}\n")
for suite in ("boot", "1v1", "team", "bunker"):
    d = os.path.join(out, suite)
    if not os.path.isdir(d): continue
    section(suite)
    txt = os.path.join(out, suite + ".txt")
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
    print("\n| match | result | per Expert (player): waves launched, base alarms, seconds under alarm, bunker entries | errors |\n|---|---|---|---|")
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
                m = re.match(r"AISTRAT\[p(\d+) f\d+\] (.*)", line)
                if m:
                    p = per.setdefault(m.group(1), [0, 0, 0, 0])
                    t = m.group(2)
                    if t.startswith("WAVE launches"): p[0] += 1
                    elif t.startswith("BASEDEF alarm"): p[1] += 1
                    elif t.startswith("BASEDEF clear after"): p[2] += int(re.match(r"BASEDEF clear after (\d+)", t).group(1))
                    elif t.startswith("BUNKER:") and " goes into " in t: p[3] += 1
                elif re.search(r"RuntimeError|Engine thread stopped|Assertion failed|Fatal error", line):
                    errors.append(line.strip()[:120])
            # keep the decisions of the Expert AIs and drop the rest of the engine output (size)
            with open(os.path.join(d, "matches", mid + ".trace.txt"), "w") as t:
                t.writelines(l for l in open(log, errors="replace") if l.startswith("AISTRAT[") or "AIMATCH" in l)
            if j.get("ok"): os.remove(log)
        stats = "; ".join(f"p{k}: {v[0]}, {v[1]}, {v[2]}, {v[3]}" for k, v in sorted(per.items())) or "-"
        print(f"| {mid} | {outcome} | {stats} | {(j.get('error') or '') + ' ' + ' / '.join(errors[:2])} |")
PY

echo
echo "Report: $OUT/summary.md"
echo "Send it:  git add test-reports && git commit -m \"Gameplay test report $COMMIT\" && git push"
exit $status
