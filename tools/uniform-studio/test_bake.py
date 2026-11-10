import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import struct

from bake import MAGIC, bake_atlas, fnv1a, read_ppu, write_ppu  # noqa: E402


def checker_rgba(w, h):
    a = np.zeros((h, w, 4), np.uint8)
    yy, xx = np.mgrid[:h, :w]
    on = ((xx // 4 + yy // 4) % 2) == 0
    a[on] = (200, 30, 40, 255)
    a[~on] = (20, 40, 160, 255)
    a[:4, :4] = 0
    return Image.fromarray(a, "RGBA")


class T(unittest.TestCase):
    def test_bake(self):
        pal, c8 = bake_atlas(checker_rgba(128, 128), "body")
        self.assertEqual(len(pal), 512)
        self.assertEqual(len(c8), 128 * 128)
        self.assertEqual(pal[0:2], b"\x00\x00")

    def test_version2_flags(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"body": checker_rgba(128, 128)}, [0, 0, 0, 0, 0, 0, 0, 0, 4, 21], flags=1)
            r = read_ppu(p)
            self.assertEqual((r["version"], r["flags"], r["backup"][8:]), (2, 1, [4, 21]))
            write_ppu(p, {"body": checker_rgba(128, 128)}, [1] * 10)
            self.assertEqual((read_ppu(p)["version"], read_ppu(p)["flags"]), (1, 0))

    def test_number_layout(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"body": checker_rgba(128, 128)}, [0] * 10, flags=1, layout=(5, -3, 4, 0, -10, 120))
            r = read_ppu(p)
            self.assertEqual((r["flags"], r["layout"]), (3, (5, -3, 4, 0, -10, 120)))
            with self.assertRaises(ValueError):
                write_ppu(p, {"body": checker_rgba(128, 128)}, [0] * 10, layout=(10, 0, 0, 0, 0, 100))

    def test_thumb_kind(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"body": checker_rgba(128, 128), "thumb": checker_rgba(64, 64)}, [1] * 10)
            r = read_ppu(p)
            self.assertEqual(set(r["atlases"]), {"body", "thumb"})
            self.assertEqual(r["atlases"]["thumb"][0:2], (64, 64))
            self.assertEqual(p.read_bytes()[16 + 10 + 5 + 512 + 128 * 128], 4)  # thumb kind id after body

    def test_roundtrip_and_corrupt(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"body": checker_rgba(128, 128), "cap": checker_rgba(64, 32)}, [1] * 10)
            r = read_ppu(p)
            self.assertEqual(r["version"], 1)
            self.assertEqual(set(r["atlases"]), {"body", "cap"})
            self.assertEqual(r["backup"], [1] * 10)
            self.assertEqual(r["atlases"]["body"][0:2], (128, 128))
            d = p.read_bytes()
            variants = {"magic": b"X" + d[1:], "sum": d[:20] + bytes([d[20] ^ 1]) + d[21:],
                        "short": d[:-7], "tiny": d[:10]}
            for name, v in variants.items():
                q = Path(t) / (name + ".ppu")
                q.write_bytes(v)
                with self.assertRaises(ValueError, msg=name):
                    read_ppu(q)

    def test_layout_bytes(self):
        img = checker_rgba(128, 128)
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"body": img}, list(range(10)))
            d = p.read_bytes()
            self.assertEqual(d[0:8], b"PPUNIF\0\0")
            self.assertEqual(d[8:12], b"\x00\x00\x00\x01")
            self.assertEqual(struct.unpack(">I", d[12:16])[0], fnv1a(d[16:]))
            self.assertEqual(d[16:26], bytes(range(10)))
            self.assertEqual(d[26], 0)                 # body kind-id
            self.assertEqual(d[27:29], b"\x80\x00")    # w=128 little-endian
            self.assertEqual(d[29:31], b"\x80\x00")    # h=128 little-endian
            self.assertEqual(len(d), 31 + 512 + 128 * 128)
            r = read_ppu(p)
            pal, px = bake_atlas(img, "body")
            self.assertEqual(r["atlases"]["body"][2], pal)
            self.assertEqual(r["atlases"]["body"][3], px)
            self.assertEqual(d[31:31 + 512], pal)
            self.assertEqual(d[31 + 512:], px)

    def test_cap_width_le(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"cap": checker_rgba(64, 32)}, [0] * 10)
            d = p.read_bytes()
            self.assertEqual(d[26], 1)
            self.assertEqual(d[27:29], b"\x40\x00")
            self.assertEqual(d[29:31], b"\x20\x00")

    def test_transparent_pixel_index_zero(self):
        _, px = bake_atlas(checker_rgba(128, 128), "body")
        # top-left 4x4 is clear; first 8x4 tile starts with pixels (0,0)..(0,3)
        self.assertEqual(px[0:4], b"\x00\x00\x00\x00")
        self.assertNotEqual(px[4:8], b"\x00\x00\x00\x00")  # opaque pixels aren't index 0

    def test_each_reject_branch(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "u.ppu"
            write_ppu(p, {"body": checker_rgba(128, 128), "cap": checker_rgba(64, 32)}, [1] * 10)
            d = p.read_bytes()
            body = d[16:]

            def mk(version=1, b=body):
                return MAGIC + struct.pack(">II", version, fnv1a(b)) + b

            def bad_body(b, msg):
                with self.assertRaisesRegex(ValueError, msg):
                    read_ppu(self._w(t, mk(b=b)))

            with self.assertRaisesRegex(ValueError, "version"):
                read_ppu(self._w(t, mk(version=3)))
            bad_body(body[:11] + b"\x7f\x00" + body[13:], "atlas header")       # body w=127
            bad_body(body[:13] + b"\x40\x00" + body[15:], "atlas header")       # body h=64
            bad_body(body[:10] + b"\x09" + body[11:], "atlas header")           # kind-id 9
            bad_body(body[:-7], "truncated")                                    # inside cap pixels
            bad_body(body[:10 + 5 + 100], "truncated")                          # inside palette
            bad_body(body[:10 + 3], "truncated")                                # inside header
            bad_body(bytes([32]) + body[1:], "backup")
            bad_body(b"\x01" * 4, "checksum")                                   # < 10 bytes

    @staticmethod
    def _w(t, data):
        q = Path(t) / "x.ppu"
        q.write_bytes(data)
        return q

    def test_wrong_size(self):
        with self.assertRaises(ValueError):
            bake_atlas(checker_rgba(64, 32), "body")


if __name__ == "__main__":
    unittest.main()
