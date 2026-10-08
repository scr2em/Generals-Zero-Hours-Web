#!/bin/sh
# Builds and runs the native decoder unit test with the sanitizers.
#   ./run_decoder_test.sh [build-dir]
# If ffmpeg is installed the decoded output is also compared with ffmpeg's.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../../../.." && pwd)
OUT=${1:-${TMPDIR:-/tmp}/web_audio_decoder_test}
mkdir -p "$OUT/ref"
g++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter \
	-I"$REPO/Core/GameEngineDevice/Include" -I"$REPO/Dependencies/minimp3" \
	"$HERE/web_audio_decoder_test.cpp" "$HERE/../WebAudioDecoder.cpp" -o "$OUT/decoder_test"
REF=""
if command -v ffmpeg >/dev/null 2>&1; then
	for f in "$HERE"/data/*; do
		ffmpeg -v error -y -i "$f" -f s16le -acodec pcm_s16le "$OUT/ref/$(basename "$f").s16"
	done
	REF="$OUT/ref"
fi
"$OUT/decoder_test" "$HERE/data" $REF
