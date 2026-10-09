# minimp3

Minimalistic MP3 decoder (single header, ISO MPEG-1/2/2.5 Layer 1, 2 and 3).

- Upstream: https://github.com/lieff/minimp3 (file `minimp3.h`, `master` at commit
  ea99364f61c14656440e8d77e9c233ccf3124633 when it was vendored)
- License: CC0 1.0 Universal (public domain dedication), see `LICENSE`.

Used by the WebAssembly audio device (Core/GameEngineDevice/Source/WebDevice/Audio) to decode the
`.mp3` music and speech of the game data, because the engine decodes on its own thread and the
browser's `decodeAudioData` is only available on the main thread and decodes whole files at once.

The header is not modified. The implementation is compiled in exactly one translation unit
(`WebAudioDecoder.cpp`) by defining `MINIMP3_IMPLEMENTATION` before including it.
