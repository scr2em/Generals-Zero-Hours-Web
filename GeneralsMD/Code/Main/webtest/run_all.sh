#!/bin/sh
# Runtime checks of the WebAssembly engine in headless Chromium.
#
#   source emsdk_env.sh
#   cmake --preset emscripten -B build/em-run && ninja -C build/em-run z_generals web_fs_test
#   PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers NODE_PATH=/opt/node22/lib/node_modules \
#       GeneralsMD/Code/Main/webtest/run_all.sh build/em-run/GeneralsMD
#
# 1. fs_test: the OPFS mounts, Windows paths, case insensitivity, directory listing.
# 2. dummy files (empty archives): the engine must stop with a clear message in the error panel.
# 3. synthetic data (gen_synthetic_data.py): start-up, frame loop, input and a clean quit.
# Output (screenshots, logs, generated data) goes to out/ and data/ next to this script, which are
# ignored by git. A debug build (-DRTS_WEB_DEBUG=ON -DRTS_DEBUG_LOGGING=ON) also logs what the engine
# loads; the release build prints only errors.
set -e
site=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"
mkdir -p out data

echo "== synthetic data"
python3 gen_synthetic_data.py data/synthetic
rm -rf data/dummy && mkdir -p data/dummy/ZeroHour data/dummy/Generals
printf 'BIGF\0\0\0\0' > data/dummy/ZeroHour/inizh.big
printf 'BIGF\0\0\0\0' > data/dummy/Generals/ini.big
python3 gen_synthetic_data.py data/fs --stage 1 --loose >/dev/null

if [ -f "$site/web_fs_test.html" ]; then
	echo "== fs_test"
	node smoke.mjs --site "$site" --page web_fs_test.html --data data/fs --until FS_DONE --shot out/fs.png --log out/fs.log --wait 30 | grep -E "FS:|RESULT"
fi

echo "== dummy files: expect a clear message and no trap"
node smoke.mjs --site "$site" --data data/dummy --shot out/dummy.png --log out/dummy.log --wait 30 | grep -E "Fatal|Required|RESULT|shown|error panel"

echo "== synthetic data: frames, input, quit"
node smoke.mjs --site "$site" --data data/synthetic --shot out/synthetic.png --log out/synthetic.log --wait 40 \
	--arg -webinputlog --input --quit | grep -E "frames:|startup:|wasm heap|input reached|quit:|loading text|RESULT"
