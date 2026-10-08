#!/bin/sh
# Builds and runs all tests of the web audio device from the root of the repository:
#   source <emsdk>/emsdk_env.sh
#   PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers Core/GameEngineDevice/Source/WebDevice/Audio/Tests/run_all.sh
#
#  1. native unit test of the decoders with the address/undefined behaviour sanitizers
#  2. web_audio_test in headless Chromium: everything (about 80 seconds)
#  3. the autoplay policy: a page without a user gesture (the first click resumes the audio) and a
#     page that starts after a click (running at once, as after the launcher's Play button)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../../../.." && pwd)
BUILD=${1:-$REPO/build/em-audio-test}

"$HERE/run_decoder_test.sh" "$BUILD/native"

emcmake cmake -S "$REPO/Core/GameEngineDevice/Source/WebDevice/Audio" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "$BUILD" web_audio_test

node "$HERE/run_test.mjs" "$BUILD/Tests" 150
node "$HERE/run_test.mjs" "$BUILD/Tests" 60 gesture
node "$HERE/run_test.mjs" "$BUILD/Tests" 60 activated
