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


# ---------------------------------------------------------------------------------------------------
# Microsoft IMA ADPCM (WAVE_FORMAT_IMA_ADPCM = 0x11), mono: four times smaller than 16 bit PCM. The web audio
# decoder (WebAudioDecoder.cpp) reads this flavour: per block a 4 byte header (int16 predictor, uint8 step
# index, 0) then 4 bit codes, low nibble first, in groups of 8 samples = 4 bytes.
# ---------------------------------------------------------------------------------------------------

_STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
          107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
          796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
          4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
          20350, 22385, 24623, 27086, 29794, 32767]
_INDEX_ADJUST = [-1, -1, -1, -1, 2, 4, 6, 8]


def _step_predictor(code, predictor, index):
    step = _STEPS[index]
    diff = step >> 3
    if code & 4:
        diff += step
    if code & 2:
        diff += step >> 1
    if code & 1:
        diff += step >> 2
    predictor += -diff if code & 8 else diff
    predictor = max(-32768, min(32767, predictor))
    index = max(0, min(88, index + _INDEX_ADJUST[code & 7]))
    return predictor, index


def _encode_sample(sample, predictor, index):
    step = _STEPS[index]
    diff = sample - predictor
    code = 0
    if diff < 0:
        code = 8
        diff = -diff
    if diff >= step:
        code |= 4
        diff -= step
    if diff >= step >> 1:
        code |= 2
        diff -= step >> 1
    if diff >= step >> 2:
        code |= 1
    predictor, index = _step_predictor(code, predictor, index)
    return code, predictor, index


def write_ima_adpcm(samples, sample_rate=22050, block_align=256):
    """Encode float samples in [-1, 1] as a mono IMA ADPCM WAV."""
    pcm = [int(round(max(-1.0, min(1.0, s)) * 32767)) for s in samples]
    per_block = (block_align - 4) * 2 + 1
    blocks = bytearray()
    pos = 0
    index = 0
    while pos < len(pcm):
        chunk = pcm[pos:pos + per_block]
        chunk += [chunk[-1]] * (per_block - len(chunk))     # pad the last block by repeating the last sample
        predictor = chunk[0]            # the step index carries over so the first samples of a block track well
        block = bytearray(struct.pack("<hBB", predictor, index, 0))
        codes = []
        for s in chunk[1:]:
            code, predictor, index = _encode_sample(s, predictor, index)
            codes.append(code)
        for i in range(0, len(codes), 2):
            block.append(codes[i] | (codes[i + 1] << 4))
        blocks += block
        pos += per_block
    fmt = struct.pack("<HHIIHHHH", 0x11, 1, sample_rate, sample_rate * block_align // per_block, block_align, 4, 2, per_block)
    fact = struct.pack("<I", len(pcm))
    out = bytearray(b"RIFF")
    out += struct.pack("<I", 4 + (8 + len(fmt)) + (8 + len(fact)) + (8 + len(blocks)))
    out += b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    out += b"fact" + struct.pack("<I", len(fact)) + fact
    out += b"data" + struct.pack("<I", len(blocks)) + blocks
    return bytes(out)


def read_ima_adpcm(data):
    """Decode a mono IMA ADPCM WAV. Returns ``(sample_rate, channels, [int16 samples])``."""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos, fmt, body, total = 12, None, None, None
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        (size,) = struct.unpack_from("<I", data, pos + 4)
        chunk = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHHHH", chunk, 0)
        elif tag == b"fact":
            (total,) = struct.unpack_from("<I", chunk, 0)
        elif tag == b"data":
            body = chunk
        pos += 8 + size + (size & 1)
    if fmt is None or body is None or fmt[0] != 0x11 or fmt[1] != 1:
        raise ValueError("not a mono IMA ADPCM file")
    block_align, per_block = fmt[4], fmt[7]
    out = []
    for start in range(0, len(body) - block_align + 1, block_align):
        block = body[start:start + block_align]
        predictor, index, _ = struct.unpack_from("<hBB", block, 0)
        out.append(predictor)
        for byte in block[4:]:
            for code in (byte & 15, byte >> 4):
                predictor, index = _step_predictor(code, predictor, index)
                out.append(predictor)
    if total is not None:
        out = out[:total]
    return fmt[2], fmt[1], out
