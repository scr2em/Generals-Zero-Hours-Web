#!/bin/sh
# Tests of the read in place mode in headless Chromium.
#
#   source emsdk_env.sh
#   cmake --preset emscripten -B build/em-direct && ninja -C build/em-direct z_generals web_direct_test
#   PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers NODE_PATH=/opt/node22/lib/node_modules \
#       GeneralsMD/Code/Main/webdirect/test/run_all.sh build/em-direct/GeneralsMD [big-file-MB]
#
# showDirectoryPicker cannot be driven headlessly, so the folder goes in through <input webkitdirectory>
# (File objects, the same thing the picker's handles produce) and the folder handle path is covered with an
# OPFS directory handle saved in IndexedDB. Generated data goes to $TMPDIR (default /tmp).
#
#  1. backend:  mount, case insensitive lookup, listing, stat, read/seek/fseek, big reads, threads, read only
#  2. changed:  a file that changes on disk after it was opened fails with EIO
#  3. handle:   save the folder handle, reload the page, reopen it (permission query/request, walk, getFile)
#  4. dummy:    empty archives: the engine must stop with a clear "missing file" message
#  5. memory:   a large file (default 1500 MB) is read completely; the browser's memory stays flat
set -e
site=$(cd "$1" && pwd)
bigmb=${2:-1500}
here=$(cd "$(dirname "$0")" && pwd)
work=${TMPDIR:-/tmp}/zhdirect-test
mkdir -p "$work"
run="node $here/run_direct.mjs --site $site"

[ -d "$work/fx40" ] || python3 "$here/make_fixture.py" "$work/fx40" --big-mb 40
rm -rf "$work/fxchange" && cp -r "$work/fx40" "$work/fxchange"
[ -d "$work/dummy" ] || { mkdir -p "$work/dummy/ZeroHour" "$work/dummy/Generals"; printf 'BIGF\0\0\0\0' > "$work/dummy/ZeroHour/inizh.big"; printf 'BIGF\0\0\0\0' > "$work/dummy/Generals/ini.big"; }

echo "== 1. backend"
$run --folder "$work/fx40" --until DIRECT_DONE --wait 90 | grep -E "FAIL|failures|stats"

echo "== 2. file changed on disk"
$run --folder "$work/fxchange" --arg -waitchange --arg -nothreads --touch "$work/fxchange/Generals/INI.big" --until DIRECT_DONE --wait 60 | grep -E "FAIL|changed|failures"

echo "== 3. saved folder handle, second visit"
$run --folder "$work/fx40" --mode handle --until DIRECT_DONE --wait 90 | grep -E "visit|FAIL|failures"

echo "== 4. dummy archives: expect the missing file message"
$run --folder "$work/dummy" --script z_generals.js --until "Fatal|Required" --wait 60 | grep -E "direct mode|Required|Fatal"

echo "== 5. memory with a ${bigmb} MB file"
[ -d "$work/fxbig" ] || python3 "$here/make_fixture.py" "$work/fxbig" --big-mb "$bigmb"
$run --folder "$work/fxbig" --arg -full --arg -nothreads --until DIRECT_DONE --wait 600 --rss | grep -E "RSS|FAIL|whole|failures|stats"
