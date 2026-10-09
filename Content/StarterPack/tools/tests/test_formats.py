"""Round trips and structural checks for the small formats: BIG, TGA, WAV/ADPCM, STR/CSF, DataChunk."""
import math
import struct
import unittest

import _path  # noqa: F401
from spk.bigfile import BigWriter, read_big
from spk.tga import write_tga, read_tga
from spk.wavfile import write_wav, read_wav, write_ima_adpcm, read_ima_adpcm
from spk.strfile import StringTable, parse_str, parse_csf
from spk.datachunk import ChunkWriter, ChunkReader, Dict, Unicode


class BigTest(unittest.TestCase):
    def test_round_trip_and_header(self):
        b = BigWriter()
        b.add("Data/INI/Object.ini", b"hello")
        b.add("art\\w3d\\a.w3d", b"\x00\x01\x02")
        data = b.to_bytes()
        self.assertEqual(data[:4], b"BIGF")
        self.assertEqual(struct.unpack_from("<I", data, 4)[0], len(data))
        self.assertEqual(struct.unpack_from(">I", data, 8)[0], 2)
        files = read_big(data)
        self.assertEqual(files["data\\ini\\object.ini"], b"hello")
        self.assertEqual(files["art\\w3d\\a.w3d"], b"\x00\x01\x02")

    def test_deterministic(self):
        def make(order):
            b = BigWriter()
            for n in order:
                b.add(n, n.encode())
            return b.to_bytes()
        self.assertEqual(make(["a", "b", "c"]), make(["c", "a", "b"]))


class TgaTest(unittest.TestCase):
    def pixels(self, w, h):
        return bytes((x * 7 + y * 3 + c * 50) & 255 if c < 3 else 255 - (x & 1) * 100
                     for y in range(h) for x in range(w) for c in range(4))

    def test_round_trip_all_variants(self):
        w, h = 13, 7
        px = self.pixels(w, h)
        for alpha in (True, False):
            for rle in (False, True):
                for origin in ("top", "bottom"):
                    data = write_tga(w, h, px, alpha=alpha, origin=origin, rle=rle)
                    rw, rh, out = read_tga(data)
                    self.assertEqual((rw, rh), (w, h))
                    want = px if alpha else bytes(v if i % 4 != 3 else 255 for i, v in enumerate(px))
                    self.assertEqual(out, want, (alpha, rle, origin))

    def test_header_matches_engine_expectations(self):
        data = write_tga(4, 4, bytes(64), alpha=True, rle=True)
        id_len, cmap, kind = data[0], data[1], data[2]
        self.assertEqual((id_len, cmap, kind), (0, 0, 10))     # no id field: WorldHeightMap::readTiles does not skip one

    def test_rle_runs_never_cross_rows(self):
        w, h = 100, 4
        data = write_tga(w, h, bytes([5, 6, 7, 255]) * (w * h), alpha=True, rle=True)
        pos, rows = 18, 0
        for _ in range(h):
            got = 0
            while got < w:
                flag = data[pos]
                n = (flag & 0x7F) + 1
                pos += 1 + (4 if flag & 0x80 else 4 * n)
                got += n
            self.assertEqual(got, w)           # a packet that ran over the row end would make this larger
            rows += 1
        self.assertEqual(pos, len(data))


class WavTest(unittest.TestCase):
    def signal(self, n=6000):
        return [0.5 * math.sin(i * 0.05) + 0.2 * math.sin(i * 0.31) for i in range(n)]

    def test_pcm_round_trip(self):
        x = self.signal(500)
        rate, ch, out = read_wav(write_wav(x, 22050))
        self.assertEqual((rate, ch, len(out)), (22050, 1, 500))
        self.assertLess(max(abs(a / 32767.0 - b) for a, b in zip(out, x)), 1e-4)

    def test_adpcm_round_trip_within_tolerance(self):
        x = self.signal()
        data = write_ima_adpcm(x, 22050)
        rate, ch, out = read_ima_adpcm(data)
        self.assertEqual((rate, ch, len(out)), (22050, 1, len(x)))
        err = [abs(a / 32767.0 - b) for a, b in zip(out[100:], x[100:])]
        rms = (sum(e * e for e in err) / len(err)) ** 0.5
        self.assertLess(rms, 0.01)
        self.assertLess(len(data), len(x) * 2 / 3)      # about a quarter of the PCM size plus headers

    def test_adpcm_header_fields(self):
        data = write_ima_adpcm(self.signal(1200), 22050, block_align=256)
        fmt = data.index(b"fmt ")
        tag, ch, rate, byte_rate, align, bits, cb, spb = struct.unpack_from("<HHIIHHHH", data, fmt + 8)
        self.assertEqual((tag, ch, rate, align, bits, cb), (0x11, 1, 22050, 256, 4, 2))
        self.assertEqual(spb, (256 - 4) * 2 + 1)         # the decoder recomputes this from blockAlign
        self.assertEqual((len(data) - data.index(b"data") - 8) % 256, 0)


class StringTest(unittest.TestCase):
    def table(self):
        t = StringTable()
        t.add("MENU:Play", "Play \"now\"\nline two")
        t.add("MENU:Quit", "Quit")
        return t

    def test_str_round_trip(self):
        data = self.table().to_str()
        self.assertNotIn(b"\r", data)                    # CRLF makes the engine see "more than one string"
        out = parse_str(data)
        self.assertEqual(out["MENU:Quit"], "Quit")
        self.assertEqual(out["MENU:Play"], "Play \"now\"\nline two")

    def test_csf_round_trip(self):
        out = parse_csf(self.table().to_csf())
        self.assertEqual(out["MENU:Quit"], "Quit")
        self.assertEqual(out["MENU:Play"], "Play \"now\"\nline two")

    def test_duplicates_rejected(self):
        t = StringTable()
        t.add("A", "x")
        with self.assertRaises(ValueError):
            t.add("a", "y")


class DataChunkTest(unittest.TestCase):
    def test_nested_chunks_and_dict(self):
        w = ChunkWriter()
        d = Dict(a=1, b=True, c=2.5, d="text")
        d.set("e", Unicode("uni"))
        with w.chunk("Outer", 3):
            w.int(-5)
            w.ascii("hi")
            with w.chunk("Inner", 1):
                w.dict(d)
                w.real(1.5)
        r = ChunkReader(w.to_bytes())
        (name, ver, off, size), = list(r.chunks())
        self.assertEqual((name, ver), ("Outer", 3))
        cur = r.cursor(off)
        self.assertEqual(cur.int(), -5)
        self.assertEqual(cur.ascii(), "hi")
        (iname, _v, ioff, isize), = list(r.chunks(cur.pos, off + size))
        c2 = r.cursor(ioff)
        out = c2.dict()
        self.assertEqual(out.get("a"), 1)
        self.assertIs(out.get("b"), True)
        self.assertAlmostEqual(out.get("c"), 2.5)
        self.assertEqual(out.get("d"), "text")
        self.assertIsInstance(out.get("e"), Unicode)
        self.assertAlmostEqual(c2.real(), 1.5)
        self.assertEqual(c2.pos, ioff + isize)

    def test_toc_ids_start_at_one_in_order_of_use(self):
        w = ChunkWriter()
        with w.chunk("B", 1):
            pass
        with w.chunk("A", 1):
            pass
        data = w.to_bytes()
        self.assertEqual(data[:4], b"CkMp")
        self.assertEqual(struct.unpack_from("<i", data, 4)[0], 2)
        r = ChunkReader(data)
        self.assertEqual(r.names, {1: "B", 2: "A"})


if __name__ == "__main__":
    unittest.main()
