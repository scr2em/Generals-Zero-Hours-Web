"""Decode the pack's audio with the engine's own C++ decoder and compare with the Python reference.

Needs a C++ compiler and the engine sources; skipped otherwise. This is what proves that the IMA ADPCM and PCM files
the generators write are accepted by ``WebAudio::Decoder`` (the decoder of the web audio device).
"""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

import _path  # noqa: F401
from gen import audio
from spk.engine_schema import find_repo_root
from spk.wavfile import write_wav, write_ima_adpcm, read_wav, read_ima_adpcm

HERE = os.path.dirname(os.path.abspath(__file__))


@unittest.skipUnless(shutil.which("g++") or shutil.which("clang++"), "no C++ compiler")
class EngineDecoderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            repo = find_repo_root()
        except RuntimeError:
            raise unittest.SkipTest("engine sources not available")
        src = os.path.join(repo, "Core/GameEngineDevice/Source/WebDevice/Audio/WebAudioDecoder.cpp")
        if not os.path.exists(src):
            raise unittest.SkipTest("decoder source not found")
        cls.tmp = tempfile.mkdtemp(prefix="starterpack-decode-")
        cls.exe = os.path.join(cls.tmp, "decode")
        cxx = shutil.which("g++") or shutil.which("clang++")
        cmd = [cxx, "-std=c++20", "-O1", "-I" + os.path.join(repo, "Core/GameEngineDevice/Include"),
               "-I" + os.path.join(repo, "Dependencies/minimp3"), os.path.join(HERE, "engine_decode_check.cpp"), src,
               "-o", cls.exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise unittest.SkipTest("decoder does not compile here: " + r.stderr[-300:])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def decode(self, name, data):
        path = os.path.join(self.tmp, name)
        with open(path, "wb") as f:
            f.write(data)
        r = subprocess.run([self.exe, path, path + ".s16"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        codec, rate, ch, frames = (int(x) for x in r.stdout.split())
        with open(path + ".s16", "rb") as f:
            raw = f.read()
        return codec, rate, ch, frames, list(struct.unpack("<%dh" % (len(raw) // 2), raw))

    def test_every_effect_decodes_to_the_same_samples(self):
        for name, make in sorted(audio.effects().items()):
            samples = make()
            codec, rate, ch, frames, pcm = self.decode(name + ".wav", write_wav(samples, audio.RATE))
            self.assertEqual((codec, rate, ch, frames), (1, audio.RATE, 1, len(samples)), name)
            self.assertEqual(pcm, read_wav(write_wav(samples, audio.RATE))[2], name)

    def test_adpcm_matches_the_python_decoder(self):
        for name, (samples, _loop) in sorted(audio.music_tracks().items()):
            data = write_ima_adpcm(samples, audio.RATE)
            codec, rate, ch, frames, pcm = self.decode(name + ".wav", data)
            ref = read_ima_adpcm(data)[2]
            self.assertEqual((codec, rate, ch), (2, audio.RATE, 1), name)
            self.assertEqual(frames, len(ref), name)
            worst = max(abs(a - b) for a, b in zip(pcm, ref))
            self.assertLessEqual(worst, 1, "%s: engine and reference decoders differ by %d" % (name, worst))


if __name__ == "__main__":
    unittest.main()
