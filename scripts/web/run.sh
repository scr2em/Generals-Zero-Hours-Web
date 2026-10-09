#!/usr/bin/env bash
# Builds and runs the web version of Zero Hour on this machine, installing what is missing.
# Safe to run again and again: every step is skipped or quick when it is already done.
#
#   scripts/web/run.sh [options]
#
#   --port N          serve on this port (default 8000)
#   --build-dir DIR   build directory (default build/web, or build/web-debug with --debug)
#   --no-video        build without FFmpeg (faster first build; the game's videos are skipped)
#   --debug           debug logging and assertions in the browser console (separate build directory)
#   --build-only      build, but do not serve
#   --no-open         serve, but do not open the browser
#   -h, --help        this text
#
# Needs: macOS with Homebrew, or Linux with apt-get (other systems: install cmake, ninja, python3
# and git yourself). The Emscripten SDK goes to $EMSDK_DIR (default ~/emsdk).
# Play in Chrome 137 or newer: http://localhost:<port>/z_generals.html
set -euo pipefail

EMSDK_VERSION=6.0.11          # keep in sync with .github/workflows/web-release.yml
EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"
PORT=8000
BUILD_DIR=""
VIDEO=ON
DEBUG=0
SERVE=1
OPEN=1

usage() { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--port) PORT="$2"; shift 2 ;;
		--build-dir) BUILD_DIR="$2"; shift 2 ;;
		--no-video) VIDEO=OFF; shift ;;
		--debug) DEBUG=1; shift ;;
		--build-only) SERVE=0; shift ;;
		--no-open) OPEN=0; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
if [ -z "$BUILD_DIR" ]; then
	if [ "$DEBUG" = 1 ]; then BUILD_DIR=build/web-debug; else BUILD_DIR=build/web; fi
fi

step() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

# 1. Build tools ---------------------------------------------------------------------------------
step "Checking the build tools (cmake, ninja, python3, git)"
missing=""
for tool in cmake ninja python3 git; do
	have "$tool" || missing="$missing $tool"
done
if [ -n "$missing" ]; then
	case "$(uname -s)" in
		Darwin)
			if ! have brew; then
				echo "Missing:$missing. Install Homebrew (https://brew.sh) and run this script again." >&2
				exit 1
			fi
			pkgs=""
			for tool in $missing; do
				case "$tool" in python3) pkgs="$pkgs python" ;; *) pkgs="$pkgs $tool" ;; esac
			done
			echo "Installing with Homebrew:$pkgs"
			# shellcheck disable=SC2086
			brew install $pkgs
			;;
		Linux)
			if have apt-get; then
				pkgs=""
				for tool in $missing; do
					case "$tool" in ninja) pkgs="$pkgs ninja-build" ;; *) pkgs="$pkgs $tool" ;; esac
				done
				SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO=sudo
				echo "Installing with apt-get:$pkgs"
				$SUDO apt-get update
				# shellcheck disable=SC2086
				$SUDO apt-get install -y $pkgs
			else
				echo "Missing:$missing. Install them with your package manager and run this script again." >&2
				exit 1
			fi
			;;
		*)
			echo "Missing:$missing. Install them and run this script again." >&2
			exit 1
			;;
	esac
fi
echo "ok"

# 2. Emscripten SDK ------------------------------------------------------------------------------
step "Emscripten SDK $EMSDK_VERSION in $EMSDK_DIR"
if [ ! -x "$EMSDK_DIR/emsdk" ]; then
	git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi
if [ ! -f "$EMSDK_DIR/.zh-installed-$EMSDK_VERSION" ]; then
	# A newer SDK version may need a newer emsdk script.
	if ! "$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"; then
		git -C "$EMSDK_DIR" pull --ff-only
		"$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
	fi
	touch "$EMSDK_DIR/.zh-installed-$EMSDK_VERSION"
fi
"$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION" >/dev/null
set +u   # emsdk_env.sh is not written for "set -u"
# shellcheck disable=SC1091
EMSDK_QUIET=1 source "$EMSDK_DIR/emsdk_env.sh" >/dev/null
set -u
emcc --version | head -n 1

# 3. Configure and build -------------------------------------------------------------------------
step "Configuring $BUILD_DIR (videos: $VIDEO, debug: $DEBUG)"
cfg=(-DRTS_WEB_FFMPEG="$VIDEO")
if [ "$DEBUG" = 1 ]; then
	cfg+=(-DRTS_DEBUG_LOGGING=ON -DRTS_DEBUG_CRASHING=ON)
fi
# Configure only when needed: configuring again rewrites a version header, which costs a relink.
cache="$BUILD_DIR/CMakeCache.txt"
need_configure=0
if [ ! -f "$cache" ]; then
	need_configure=1
else
	grep -q "^RTS_WEB_FFMPEG:BOOL=$VIDEO\$" "$cache" || need_configure=1
	if [ "$DEBUG" = 1 ]; then
		grep -q "^RTS_DEBUG_LOGGING:BOOL=ON\$" "$cache" || need_configure=1
	fi
fi
if [ "$need_configure" = 1 ]; then
	cmake --preset emscripten -B "$BUILD_DIR" "${cfg[@]}" >/dev/null
else
	echo "already configured"
fi

step "Building the game and the free starter content (the first build takes a while)"
cmake --build "$BUILD_DIR" --target z_generals starter_pack

SITE="$BUILD_DIR/GeneralsMD"
URL="http://localhost:$PORT/z_generals.html"
echo
echo "Built: $SITE/z_generals.html"
[ "$SERVE" = 1 ] || exit 0

# 4. Serve ---------------------------------------------------------------------------------------
# serve.py sends the cross-origin isolation headers the game needs (threads); a plain web server will not work.
step "Serving on $URL (Ctrl+C to stop). Use Chrome 137 or newer."
if [ "$OPEN" = 1 ]; then
	(
		sleep 1
		case "$(uname -s)" in
			Darwin) open -a "Google Chrome" "$URL" 2>/dev/null || open "$URL" ;;
			Linux) (have google-chrome && google-chrome "$URL") || (have xdg-open && xdg-open "$URL") || true ;;
		esac
	) >/dev/null 2>&1 &
fi
exec python3 GeneralsMD/Code/Main/web/serve.py --port "$PORT" --dir "$SITE"
