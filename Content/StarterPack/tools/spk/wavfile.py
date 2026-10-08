"""RIFF/WAVE writer and reader (16 bit PCM).

This is the format the web audio device decodes natively
(``WebAudio::Decoder``: RIFF/WAVE PCM 8/16/24/32 bit, float, IMA ADPCM).
"""

import struct


def write_wav(samples, sample_rate=22050, channels=1):
    """Serialise float samples in [-1, 1] (interleaved if stereo) as 16 bit PCM."""
    pcm = bytearray()
    for s in samples:
        v = int(round(max(-1.0, min(1.0, s)) * 32767))
        pcm += struct.pack("<h", v)
    block_align = channels * 2
    fmt = struct.pack("<HHIIHH", 1, channels, sample_rate, sample_rate * block_align, block_align, 16)
    out = bytearray()
    out += b"RIFF"
    out += struct.pack("<I", 4 + (8 + len(fmt)) + (8 + len(pcm)))
    out += b"WAVE"
    out += b"fmt " + struct.pack("<I", len(fmt)) + fmt
    out += b"data" + struct.pack("<I", len(pcm)) + pcm
    return bytes(out)


def read_wav(data):
    """Parse a PCM WAV. Returns ``(sample_rate, channels, [int16 samples])``."""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos = 12
    fmt = None
    samples = None
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        (size,) = struct.unpack_from("<I", data, pos + 4)
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif tag == b"data":
            count = len(body) // 2
            samples = list(struct.unpack("<%dh" % count, body[:count * 2]))
        pos += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise ValueError("missing fmt or data chunk")
    audio_format, channels, rate = fmt[0], fmt[1], fmt[2]
    if audio_format != 1 or fmt[5] != 16:
        raise ValueError("only 16 bit PCM supported")
    return rate, channels, samples
