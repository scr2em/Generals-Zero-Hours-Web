#!/usr/bin/env python3
# Generates the synthetic sound files of the web audio tests (data/*.wav, data/*.mp3).
#
#   python3 gen_test_audio.py [output-dir]
#
# The files are pure sine tones made here (nothing from the game), so they are free of any
# copyright. The tone frequencies are what the tests check for:
#
#   pcm16_mono_44100_440.wav    PCM 16 bit mono, 44100 Hz, 1.0 s, 440 Hz
#   pcm8_mono_22050_550.wav     PCM 8 bit mono, 22050 Hz, 0.5 s, 550 Hz
#   adpcm_mono_22050_880.wav    IMA ADPCM mono, 22050 Hz, 1.5 s, 880 Hz   (own encoder below)
#   adpcm_stereo_44100_660.wav  IMA ADPCM stereo, 44100 Hz, 1.0 s, 660 Hz left / 990 Hz right
#   music_stereo_44100_1320.mp3 MP3 stereo, 44100 Hz, 4.0 s, 1320 Hz left / 1760 Hz right (LAME, needs ffmpeg)
#   music_mono_22050_1100.mp3   MP3 mono, 22050 Hz (MPEG-2), 2.0 s, 1100 Hz (LAME, needs ffmpeg)
#
# The MP3 files are checked in next to this script so that the tests do not need an MP3
# encoder; this script only needs ffmpeg with libmp3lame to regenerate them.

import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def sine(freq, rate, seconds, amp=0.5):
    n = int(rate * seconds)
    return [int(round(amp * 32767 * math.sin(2 * math.pi * freq * i / rate))) for i in range(n)]


def wav(fmt, extra, data, fact=None):
    fmt_chunk = b'fmt ' + struct.pack('<I', len(fmt) + len(extra)) + fmt + extra
    fact_chunk = b'fact' + struct.pack('<II', 4, fact) if fact is not None else b''
    body = b'WAVE' + fmt_chunk + fact_chunk + b'data' + struct.pack('<I', len(data)) + data
    return b'RIFF' + struct.pack('<I', len(body)) + body


def pcm16(samples_per_channel, rate):
    channels = len(samples_per_channel)
    inter = [s for frame in zip(*samples_per_channel) for s in frame]
    data = struct.pack('<%dh' % len(inter), *inter)
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * channels * 2, channels * 2, 16)
    return wav(fmt, b'', data)


def pcm8(samples, rate):
    data = bytes((s >> 8) + 128 for s in samples)
    fmt = struct.pack('<HHIIHH', 1, 1, rate, rate, 1, 8)
    return wav(fmt, b'', data)


class ImaEncoder:
    def __init__(self):
        self.pred = 0
        self.index = 0

    def encode(self, sample):
        step = STEP[self.index]
        diff = sample - self.pred
        nib = 0
        if diff < 0:
            nib = 8
            diff = -diff
        if diff >= step:
            nib |= 4
            diff -= step
        step >>= 1
        if diff >= step:
            nib |= 2
            diff -= step
        step >>= 1
        if diff >= step:
            nib |= 1
        # reconstruct exactly like the decoder
        step = STEP[self.index]
        d = step >> 3
        if nib & 1: d += step >> 2
        if nib & 2: d += step >> 1
        if nib & 4: d += step
        if nib & 8: d = -d
        self.pred = max(-32768, min(32767, self.pred + d))
        self.index = max(0, min(88, self.index + INDEX[nib & 7]))
        return nib


def ima_adpcm(samples_per_channel, rate, block_align):
    channels = len(samples_per_channel)
    spb = (block_align - 4 * channels) * 2 // channels + 1
    total = len(samples_per_channel[0])
    blocks = []
    pos = 0
    while pos < total:
        chunk = [ch[pos:pos + spb] for ch in samples_per_channel]
        n = len(chunk[0])
        # a short last block is padded with its final sample, as encoders do
        full = spb if n == spb else 1 + ((n - 1 + 7) // 8) * 8
        chunk = [c + [c[-1]] * (full - n) for c in chunk]
        enc = [ImaEncoder() for _ in range(channels)]
        out = bytearray()
        for c in range(channels):
            enc[c].pred = chunk[c][0]
            # index chosen as the encoder state continues; start from 0 for the first block, carry on after
            out += struct.pack('<hBB', chunk[c][0], enc[c].index, 0)
        for g in range((full - 1) // 8):
            for c in range(channels):
                nibs = [enc[c].encode(chunk[c][1 + g * 8 + i]) for i in range(8)]
                out += bytes(nibs[i * 2] | (nibs[i * 2 + 1] << 4) for i in range(4))
        blocks.append(bytes(out))
        pos += spb
    # Make all blocks the same size (the last one may be shorter, which the format allows).
    data = b''.join(blocks)
    fmt = struct.pack('<HHIIHH', 0x11, channels, rate, rate * block_align // spb, block_align, 4)
    extra = struct.pack('<HH', 2, spb)
    return wav(fmt, extra, data, fact=total)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
    os.makedirs(out, exist_ok=True)

    def put(name, blob):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(blob)
        print('wrote', name, len(blob), 'bytes')

    put('pcm16_mono_44100_440.wav', pcm16([sine(440, 44100, 1.0)], 44100))
    put('pcm8_mono_22050_550.wav', pcm8(sine(550, 22050, 0.5), 22050))
    put('adpcm_mono_22050_880.wav', ima_adpcm([sine(880, 22050, 1.5)], 22050, 512))
    put('adpcm_stereo_44100_660.wav', ima_adpcm([sine(660, 44100, 1.0), sine(990, 44100, 1.0)], 44100, 1024))

    if shutil.which('ffmpeg'):
        with tempfile.TemporaryDirectory() as tmp:
            for name, rate, freqs, secs, br in (
                ('music_stereo_44100_1320.mp3', 44100, (1320, 1760), 4.0, '64k'),
                ('music_mono_22050_1100.mp3', 22050, (1100,), 2.0, '32k'),
            ):
                src = os.path.join(tmp, 'src.wav')
                with open(src, 'wb') as f:
                    f.write(pcm16([sine(fr, rate, secs) for fr in freqs], rate))
                dst = os.path.join(out, name)
                subprocess.check_call(['ffmpeg', '-v', 'error', '-y', '-i', src, '-c:a', 'libmp3lame', '-b:a', br,
                                       '-write_xing', '1', '-id3v2_version', '0', '-write_id3v1', '0', dst])
                print('wrote', name, os.path.getsize(dst), 'bytes')
    else:
        print('ffmpeg not found: the checked-in MP3 files are kept')


if __name__ == '__main__':
    main()
