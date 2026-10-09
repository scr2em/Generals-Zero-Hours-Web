#!/usr/bin/env python3
"""Writes a small Bink 1 (BIKi) video for testing the video playback of the web build.

The game's real videos are copyrighted and FFmpeg has no Bink encoder, so this file is a
minimal Bink encoder of our own: every 8x8 block of a frame is a FILL block (one value per
block), and the audio track is a sine tone in the RDFT audio format. The file is valid Bink
that the FFmpeg decoder (and the real players) accept, and what it decodes to is known exactly,
so the tests can check the decoded frames and samples against the sidecar <name>.json.

Everything here is original work (no data from any Bink file), so the output may be
redistributed; it is generated at test time and is not kept in the repository.

  make_test_bik.py out.bik [--width 320 --height 240 --frames 90 --fps 30]
                           [--fps-den 1] [--rate 22050 --channels 1 --audio rdft|dct|none]
"""
import argparse
import json
import math
import struct
import sys

# ---- bit writer (the Bink bit stream is little endian: first bit = lowest bit of a byte) ------

class BitWriter:
    def __init__(self):
        self.bits = 0
        self.n = 0

    def put(self, value, count):
        assert 0 <= value < (1 << count) or count == 0
        self.bits |= value << self.n
        self.n += count

    def align32(self):
        if self.n & 31:
            self.n += 32 - (self.n & 31)

    def tobytes(self):
        self.align32()
        return self.bits.to_bytes(self.n // 8, 'little')


def log2(v):
    return v.bit_length() - 1


# ---- video ---------------------------------------------------------------------------------------

FILL_BLOCK = 6


def plane_bundle_lengths(width, bw):
    aligned = (max(width, 8) + 7) & ~7
    return {
        'types': log2((aligned >> 3) + 511) + 1,
        'colors': log2(bw * 64 + 511) + 1,
        'zero': log2((aligned >> 3) + 511) + 1,      # x/y offsets and the DC bundles
        'sub': log2((aligned >> 4) + 511) + 1,
        'pattern': log2((bw << 3) + 511) + 1,
        'run': log2(bw * 48 + 511) + 1,
    }


def encode_plane(w, values, width, bw, bh):
    """values[by][bx] = the fill value of the block."""
    ln = plane_bundle_lengths(width, bw)
    # The trees of the 9 bundles: tree 0 (identity, 4 bit codes) for all. 23 trees in total:
    # 7 bundle trees + the 16 trees of the high nibble of the colors.
    w.put(0, 4 * 23)
    for by in range(bh):
        # block types: bw blocks, a single run of FILL_BLOCK
        w.put(bw, ln['types'])
        w.put(1, 1)
        w.put(FILL_BLOCK, 4)
        if by == 0:
            w.put(0, ln['sub'])  # no 16x16 sub block types (and none for the rest of the plane)
        # colors: bw values, each a high and a low nibble with tree 0
        w.put(bw, ln['colors'])
        w.put(0, 1)
        for bx in range(bw):
            v = values[by][bx]
            w.put(v >> 4, 4)
            w.put(v & 15, 4)
        if by == 0:
            for key in ('pattern', 'zero', 'zero', 'zero', 'zero', 'run'):
                w.put(0, ln[key])  # patterns, x, y, intra dc, inter dc, runs: none
    w.align32()


def block_value(plane, bx, by, n):
    if plane == 0:
        return 16 + (bx * 7 + by * 5 + n * 3) % 200
    if plane == 1:   # U
        return 90 + (n * 2) % 100
    return 200 - (n * 2) % 100   # V


def encode_frame(width, height, n):
    w = BitWriter()
    w.put(0, 32)  # versions i and later: 32 unused bits first
    bw = (width + 7) >> 3
    bh = (height + 7) >> 3
    cbw = (width + 15) >> 4
    cbh = (height + 15) >> 4
    # Y, then V, then U (the planes are swapped from version h on)
    encode_plane(w, [[block_value(0, x, y, n) for x in range(bw)] for y in range(bh)], width, bw, bh)
    encode_plane(w, [[block_value(2, x, y, n) for x in range(cbw)] for y in range(cbh)], width // 2, cbw, cbh)
    encode_plane(w, [[block_value(1, x, y, n) for x in range(cbw)] for y in range(cbh)], width // 2, cbw, cbh)
    return w.tobytes()


# ---- audio ---------------------------------------------------------------------------------------

ROOT_CACHE = {}


def audio_params(rate, channels):
    """Frame length of the RDFT/DCT audio decoder (see libavcodec/binkaudio.c)."""
    bits = 9 if rate < 22050 else (10 if rate < 44100 else 11)
    return bits


WMA_CRITICAL_FREQS = [100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000, 2320,
                      2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500]


def num_bands(rate_effective):
    half = (rate_effective + 1) // 2
    n = 1
    while n < 25:
        if half <= WMA_CRITICAL_FREQS[n - 1]:
            break
        n += 1
    return n


def put_float(w, f):
    """get_float(): 5 bit power, 23 bit mantissa, sign. Only 0 is needed here."""
    assert f == 0
    w.put(0, 5)
    w.put(0, 23)
    w.put(0, 1)


def encode_audio_block(rate, channels, dct, tone_bin, amplitude_idx, coeff_value, frame_len, bands_n, silent=False):
    """One block (rdft: all channels interleaved in one signal) with a single tone."""
    w = BitWriter()
    if dct:
        w.put(0, 2)
    put_float(w, 0)
    put_float(w, 0)
    for _ in range(bands_n):
        w.put(amplitude_idx, 8)
    i = 2
    target = 2 * tone_bin  # index of the real part of the bin
    while i < frame_len:
        w.put(0, 1)  # a group of 8 coefficients
        j = min(i + 8, frame_len)
        if i <= target < j and not dct_silence(dct) and not silent:
            w.put(4, 4)  # 4 bits per coefficient
            for k in range(i, j):
                if k == target:
                    w.put(coeff_value, 4)
                    w.put(0, 1)  # positive
                else:
                    w.put(0, 4)
        else:
            w.put(0, 4)  # width 0: all zero
        i = j
    return w.tobytes()


def dct_silence(dct):
    # The DCT variant is only written as silence (its output layout is not worth modelling here).
    return dct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--width', type=int, default=320)
    ap.add_argument('--height', type=int, default=240)
    ap.add_argument('--frames', type=int, default=90)
    ap.add_argument('--fps', type=int, default=30, help='frame rate numerator')
    ap.add_argument('--fps-den', type=int, default=1, help='frame rate denominator (30000 with 1001 is NTSC video)')
    ap.add_argument('--rate', type=int, default=22050)
    ap.add_argument('--channels', type=int, default=1, choices=(1, 2))
    ap.add_argument('--audio', default='rdft', choices=('rdft', 'dct', 'none'))
    ap.add_argument('--tone-bin', type=int, default=24, help='frequency bin of the tone (rdft)')
    ap.add_argument('--tone-window', default='', help='START:END seconds: the tone is only audible in this window, '
                    'silence elsewhere (a marker for checking the sync of picture and sound)')
    args = ap.parse_args()

    W, H, N, fps = args.width, args.height, args.frames, args.fps / args.fps_den
    video = [encode_frame(W, H, n) for n in range(N)]

    use_audio = args.audio != 'none'
    dct = args.audio == 'dct'
    audio_packets = [b''] * N
    total_samples = 0
    block_samples = 0
    if use_audio:
        bits = audio_params(args.rate, args.channels)
        rate_eff = args.rate
        if not dct:
            rate_eff *= args.channels
            bits += int(math.log2(args.channels))
        frame_len = 1 << bits
        overlap = frame_len // 16
        # samples per block per channel: rdft: (frame_len - overlap) of the interleaved signal
        # decoded samples per block and channel (rdft: one interleaved signal; dct: a block per channel)
        block_samples = (frame_len - overlap) // (args.channels if not dct else 1)
        bands_n = num_bands(rate_eff)
        # Calibrated on the decoder: coefficient 15 with quantizer index 64 gives about 0.5 of full scale.
        tone_block = encode_audio_block(args.rate, args.channels, dct, args.tone_bin, 64, 15, frame_len, bands_n)
        silent_block = encode_audio_block(args.rate, args.channels, True if dct else False, args.tone_bin, 64, 0, frame_len, bands_n,
                                          silent=True)
        window = None
        if args.tone_window:
            a, b = args.tone_window.split(':')
            window = (float(a), float(b))
        produced = 0.0
        need_per_frame = args.rate / fps
        for n in range(N):
            # Add blocks until the audio covers this frame and the next (about 1 frame ahead).
            blocks = []
            while produced < (n + 1) * need_per_frame:
                start_sec = produced / args.rate
                audible = window is None or window[0] <= start_sec < window[1]
                blocks.append(tone_block if audible else silent_block)
                produced += block_samples
            count = len(blocks)
            if count:
                payload = struct.pack('<I', count * block_samples * 2 * args.channels) + b''.join(blocks)
                audio_packets[n] = payload
                total_samples += count * block_samples

    # ---- container ----
    tracks = 1 if use_audio else 0
    header_size = 44 + (tracks * (4 + 4 + 4)) + 4 * N
    # fixed header: 0..43 (signature to number of audio tracks)
    frames = []
    for n in range(N):
        data = b''
        if tracks:
            ap_ = audio_packets[n]
            data += struct.pack('<I', len(ap_)) + ap_
        data += video[n]
        frames.append(data)
    pos = header_size
    offsets = []
    for f in frames:
        offsets.append(pos)
        pos += len(f)
    file_size = pos
    largest = max(len(f) for f in frames)

    out = bytearray()
    out += b'BIKi'
    out += struct.pack('<I', file_size - 8)
    out += struct.pack('<I', N)
    out += struct.pack('<I', largest)
    out += struct.pack('<I', N)
    out += struct.pack('<II', W, H)
    out += struct.pack('<II', args.fps, args.fps_den)
    out += struct.pack('<I', 0)  # video flags
    out += struct.pack('<I', tracks)
    if tracks:
        out += struct.pack('<I', 0x10000)  # max decoded size
        flags = 0x4000  # 16 bit
        if args.channels == 2:
            flags |= 0x2000
        if dct:
            flags |= 0x1000
        out += struct.pack('<HH', args.rate, flags)
        out += struct.pack('<I', 0)  # track id
    for i, off in enumerate(offsets):
        out += struct.pack('<I', off | (1 if i == 0 else 0))
    assert len(out) == header_size, (len(out), header_size)
    for f in frames:
        out += f
    open(args.out, 'wb').write(out)

    # What the video decodes to: the value of the block at (0, 0) of each plane and a few others.
    expect = {
        'width': W, 'height': H, 'frames': N, 'fps': fps, 'fps_num': args.fps, 'fps_den': args.fps_den,
        'audio': {
            'tracks': tracks, 'rate': args.rate, 'channels': args.channels,
            'codec': ('binkaudio_dct' if dct else 'binkaudio_rdft') if use_audio else None,
            'samples_per_channel': total_samples if use_audio else 0,
            'silent': dct,
            'tone_hz': (args.tone_bin * rate_eff / frame_len) if use_audio and not dct else 0,
            'tone_window': [float(x) for x in args.tone_window.split(':')] if args.tone_window else None,
        },
        'blocks': [
            {'n': n, 'y': [block_value(0, bx, by, n) for (bx, by) in ((0, 0), (1, 0), (0, 1), (3, 2))],
             'u': block_value(1, 0, 0, n), 'v': block_value(2, 0, 0, n)}
            for n in range(N)
        ],
    }
    with open(args.out + '.json', 'w') as f:
        json.dump(expect, f)
    print('wrote %s: %dx%d, %d frames at %d fps, audio %s, %d bytes' % (
        args.out, W, H, N, fps, args.audio, file_size))


if __name__ == '__main__':
    sys.exit(main())
