# FFmpegWeb: the game's videos in the browser

The game's videos are Bink 1 files (`.bik`). On Windows they play through the proprietary Bink SDK; the web build
plays them with a minimal [FFmpeg](https://ffmpeg.org) built for WebAssembly by this directory's `CMakeLists.txt`
(licence notes: `NOTICE.md`).

| Part | Where |
| --- | --- |
| FFmpeg n7.1.1 (one two line patch), LGPL, Bink demuxer + video decoder + both audio decoders + swscale, built once per build directory | `CMakeLists.txt` (target `deps_ffmpeg_web`, built by `ffmpeg_web_build`) |
| Container and decoder access through the engine's `File` API | `Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegFile.cpp` (shared with the fork's FFmpeg player) |
| Movie lookup (`Data\<language>\Movies`, `Data\Movies`, mod directory) | `FFmpegVideoPlayer::open` (shared) |
| Playback clock, frame dropping, decode ahead, the sound on the Web Audio device | `Core/GameEngineDevice/Source/WebDevice/Video/WebVideoPlayer.cpp` |
| Picture: `VideoStream::frameRender` converts the frame to the `VideoBuffer` of the W3D display (a texture) | `WebVideoStream::frameRender`, `W3DVideoBuffer` |

Options: `-DRTS_WEB_FFMPEG=OFF` leaves FFmpeg out (videos are skipped, as before), `-DRTS_WEB_FFMPEG_SOURCE_DIR=<checkout of
FFmpeg n7.1.1>` builds from a checkout instead of cloning (no network needed). The first build of a build directory clones and
compiles FFmpeg (a few minutes); later builds reuse it.

## Tests

None of the game's videos may be used (they are copyrighted), and FFmpeg cannot write Bink. `test/make_test_bik.py` is a small
Bink encoder of our own (8x8 fill blocks and a sine tone in the RDFT audio format), so the tests run on files that are generated
and are known exactly.

```sh
# 1. The library: decode generated files with the FFmpeg built for WebAssembly (under node) and compare with what was encoded
cmake --build <build dir> --target ffmpeg_web_decode_test
python3 Dependencies/FFmpegWeb/test/run_decode_test.py --runner "node <build dir>/Dependencies/FFmpegWeb/ffmpeg_web_decode_test.js"

# 2. The game: the generated video as the intro movie of the starter content, in headless Chromium
cmake --build <build dir> --target z_generals starter_pack
NODE_PATH=/opt/node22/lib/node_modules node Dependencies/FFmpegWeb/test/engine_video_test.mjs --site <build dir>/GeneralsMD --out /tmp/video-test
```

The second test plays a 6 s video as the intro movie and checks the picture (screenshots matched to the frames they show: the
frame rate on screen, the order), the sound (an analyser on the Web Audio output: the tone, when it starts, how long it lasts),
the end of the movie, skipping it with Escape, a browser without Web Audio, and that a missing, a corrupt and a truncated video
are skipped without a trap. `--save-shots` keeps the screenshots.
