# FFmpeg in the web build: licence notice

The WebAssembly build of the game plays the game's Bink videos (`.bik`) with **FFmpeg**
(<https://ffmpeg.org>), release **n7.1.1** (git commit `db69d06eeeab4f46da15030a80d539efb4503ca8`).

* FFmpeg is © the FFmpeg developers. This build contains **only LGPL code** (`--enable-gpl`,
  `--enable-version3` and `--enable-nonfree` are not used, no external libraries are linked), so
  FFmpeg is licensed under the **GNU Lesser General Public License, version 2.1 or later**
  (`configure` reports "License: LGPL version 2.1 or later"). The licence text is in the FFmpeg
  source tree (`COPYING.LGPLv2.1`) and at <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>.
* The sources are **not copied into this repository** and are used as released, with a single two line
  modification: `patches/avutil-tx-float-only.patch` removes the double precision and 32 bit integer
  transform tables from libavutil's `av_tx` lookup (the Bink audio decoders use only the float
  transforms; the tables are 12.6 MB of zero initialised memory). `CMakeLists.txt` in this directory
  clones the pinned tag from <https://github.com/FFmpeg/FFmpeg> when the web build is first built,
  applies the patch and compiles it with Emscripten. The configure line is in `CMakeLists.txt` (`_ffmpeg_configure`);
  it enables the Bink demuxer, the Bink video decoder, the two Bink audio decoders (RDFT and DCT),
  libswscale (YUV to RGB) and what they depend on in libavutil, and nothing else.
* The static libraries (`libavformat.a`, `libavcodec.a`, `libswscale.a`, `libavutil.a`) are linked
  into the game's WebAssembly module. This game is free software under the GNU General Public License
  version 3 (see `LICENSE.md`), which is compatible with the LGPL: the complete source of the game, of
  this build setup and (through the pinned tag) of FFmpeg is available, so anybody can rebuild the game
  with a modified or newer FFmpeg: change `FFMPEG_WEB_TAG` in `CMakeLists.txt`, or point
  `RTS_WEB_FFMPEG_SOURCE_DIR` at a checkout of their own.
* Bink is a trademark of RAD Game Tools. FFmpeg's Bink decoders are independent implementations
  (written from the file format, not from the Bink SDK); no Bink SDK code is used in the web build.
  The Windows build links the proprietary Bink SDK separately (`Dependencies/Bink`), which the web
  build does not.

If you distribute a build of the web game, keep this notice with it and make the FFmpeg source and
the build instructions (this directory) available, as the LGPL requires.
