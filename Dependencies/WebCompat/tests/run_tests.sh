#!/bin/sh
# Builds and runs the WebCompat runtime test with node. Not part of the game build.
# Usage: run_tests.sh   (after sourcing emsdk_env.sh)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="${TMPDIR:-/tmp}/webcompat_tests"
mkdir -p "$out"
src="$here/../src"
em++ -std=c++20 -O1 -g -pthread -fshort-wchar -fms-extensions -fno-strict-aliasing \
	-Wno-microsoft -Wno-format \
	-I"$here/../include" -include "$here/../include/web_prelude.h" \
	"$here/test_webcompat.cpp" "$src"/*.cpp \
	-sGLOBAL_BASE=1048576 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB -sPTHREAD_POOL_SIZE=8 -sEXIT_RUNTIME=1 \
	-o "$out/test_webcompat.js"
cd "$out"
node test_webcompat.js
