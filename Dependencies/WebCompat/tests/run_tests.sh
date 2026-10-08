#!/bin/sh
# Builds and runs the WebCompat runtime test with node. Not part of the game build.
# Usage: run_tests.sh   (after sourcing emsdk_env.sh)
#
# The GDI text code needs stb_truetype.h (fetched by cmake/stb.cmake): set STB_DIR to the folder that
# holds it, or let the script look in the build folders of a configured tree. The bundled fonts are
# embedded at /webfonts like in the game.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out="${TMPDIR:-/tmp}/webcompat_tests"
mkdir -p "$out"
src="$here/../src"

if [ -z "$STB_DIR" ]; then
	for candidate in "$root"/build/*/_deps/stb-src; do
		if [ -f "$candidate/stb_truetype.h" ]; then STB_DIR="$candidate"; break; fi
	done
fi
if [ ! -f "$STB_DIR/stb_truetype.h" ]; then
	echo "stb_truetype.h not found: set STB_DIR (configure any emscripten build to fetch it)" >&2
	exit 2
fi

fonts="$root/Dependencies/WebFonts/fonts"
em++ -std=c++20 -O1 -g -pthread -fshort-wchar -fms-extensions -fno-strict-aliasing \
	-Wno-microsoft -Wno-format \
	-I"$here/../include" -I"$STB_DIR" -include "$here/../include/web_prelude.h" \
	"$here/test_webcompat.cpp" "$here/test_gdi_text.cpp" "$src"/*.cpp \
	-sGLOBAL_BASE=1048576 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB -sPTHREAD_POOL_SIZE=8 -sEXIT_RUNTIME=1 \
	--embed-file "$fonts/LiberationSans-Regular.ttf@/webfonts/LiberationSans-Regular.ttf" \
	--embed-file "$fonts/LiberationSans-Bold.ttf@/webfonts/LiberationSans-Bold.ttf" \
	--embed-file "$fonts/LiberationSerif-Regular.ttf@/webfonts/LiberationSerif-Regular.ttf" \
	--embed-file "$fonts/LiberationSerif-Bold.ttf@/webfonts/LiberationSerif-Bold.ttf" \
	--embed-file "$fonts/LiberationMono-Regular.ttf@/webfonts/LiberationMono-Regular.ttf" \
	--embed-file "$fonts/LiberationMono-Bold.ttf@/webfonts/LiberationMono-Bold.ttf" \
	-o "$out/test_webcompat.js"
cd "$out"
node test_webcompat.js
