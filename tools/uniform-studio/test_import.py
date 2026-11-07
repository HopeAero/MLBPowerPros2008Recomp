# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-checks for import_sources.py (synthetic data only, no game files)."""
import struct
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import import_sources as imp

# Mid-tone RGB of the game's 32 colours, copied here as data.
GAME_PALETTE = [
    (160, 160, 160), (110, 110, 110), (90, 90, 90), (65, 65, 65), (45, 45, 45), (37, 35, 88),
    (45, 43, 150), (43, 95, 130), (54, 120, 140), (0, 65, 50), (0, 80, 50), (0, 95, 55),
    (0, 120, 0), (165, 165, 80), (188, 144, 0), (185, 110, 0), (215, 130, 119), (181, 109, 74),
    (130, 50, 30), (200, 80, 0), (160, 45, 0), (140, 45, 0), (90, 30, 40), (205, 96, 127),
    (184, 53, 136), (120, 0, 150), (70, 0, 145), (114, 47, 225), (115, 75, 255), (0, 64, 66),
    (0, 98, 98), (55, 152, 135),
]


def qfs_literal(data):
    """Minimal RefPack encoder: literal runs only (length padded to multiple of 4)."""
    out = bytearray([0x10, 0xFB]) + len(data).to_bytes(3, "big")
    p = 0
    while len(data) - p > 3:
        n = min(112, (len(data) - p) // 4 * 4)
        out.append(0xE0 + ((n - 4) >> 2))
        out += data[p:p + n]
        p += n
    rest = data[p:]
    out.append(0xFC + len(rest))
    out += rest
    return bytes(out)


def make_fsh(entries):
    """entries: list of (name, type, w, h, pixels)."""
    n = len(entries)
    pos = 16 + 8 * n
    body, table = b"", b""
    for name, t, w, h, px in entries:
        table += struct.pack("<4sI", name.encode().ljust(4), pos + len(body))
        body += bytes([t]) + b"\0\0\0" + struct.pack("<HH", w, h) + bytes(8) + px
    return struct.pack("<4sII4s", b"SHPI", 16 + len(table) + len(body), n, b"GIMX") + table + body


def test_fsh_qfs_chest():
    px = struct.pack("<H", 0xF800) * 16  # 4x4 red RGB565
    raw = make_fsh([("chst", 0x78, 4, 4, px)])
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "a.fsh"
        f.write_bytes(qfs_literal(raw))
        assert imp.unqfs(f.read_bytes()) == raw
        layers = imp.layers_from_fsh(f)
    assert len(layers) == 1
    assert layers[0]["suggest"] == "body/chest"
    assert layers[0]["rgba"].size == (4, 4)
    assert layers[0]["rgba"].getpixel((0, 0)) == (255, 0, 0, 255)


def test_fsh_names_and_formats():
    argb = struct.pack("<H", 0xF0F0) * 4  # 2x2 A=F R=0 G=F B=0
    bgra = bytes((0, 0, 255, 255)) * 4
    raw = make_fsh([("hat", 0x6D, 2, 2, argb), ("zig3", 0x7D, 2, 2, bgra), ("zzzz", 0x7D, 2, 2, bgra)])
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "b.fsh"
        f.write_bytes(raw)
        ls = imp.layers_from_fsh(f)
    assert [l["name"] for l in ls] == ["hat", "zig3", "zzzz"]
    assert [l["suggest"] for l in ls] == ["cap", "number", ""]
    assert ls[0]["rgba"].getpixel((0, 0)) == (0, 255, 0, 255)
    assert ls[1]["rgba"].getpixel((0, 0)) == (255, 0, 0, 255)


def test_iff_inline_dxt5():
    w = h = 8
    block = bytes([0xFF, 0xFF]) + bytes(6) + bytes([0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0])
    data = block * ((w // 4) * (h // 4))
    hdr = bytearray(imp.IFF_TEX_HDR)
    hdr[8:12] = b"DXT5"
    hdr[28:32] = b"DXT5"
    struct.pack_into(">I", hdr, 0x64, 0x021A0000)
    struct.pack_into("<HH", hdr, 0x68, w, h)
    d = bytearray(imp.IFF_BLOCK0) + hdr + data
    struct.pack_into("<I", d, 0, imp.IFF_MAGIC)
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "u.iff"
        f.write_bytes(bytes(d))
        ls = imp.layers_from_iff(f)
    assert len(ls) == 1 and ls[0]["rgba"].size == (8, 8)
    assert ls[0]["rgba"].getpixel((0, 0)) == (255, 0, 0, 255)
    assert ls[0]["rgba"].getpixel((7, 7)) == (255, 0, 0, 255)


def test_fsh_dxt3():
    block = bytes([0xFF] * 8) + bytes([0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0])  # alpha 15, red
    raw = make_fsh([("hat", 0x61, 4, 4, block)])
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "d.fsh"
        f.write_bytes(raw)
        ls = imp.layers_from_fsh(f)
    assert len(ls) == 1
    assert ls[0]["rgba"].getpixel((0, 0)) == (255, 0, 0, 255)
    assert ls[0]["rgba"].getpixel((3, 3)) == (255, 0, 0, 255)


def _qfs_stream(parts):
    """Hand-encode RefPack ops; return (stream, expected) with an independent simulator."""
    body, exp = bytearray(), bytearray()
    for kind, a, b in parts:
        if kind == "lit":  # a = bytes (multiple of 4)
            body.append(0xE0 + ((len(a) - 4) >> 2))
            body += a
            exp += a
            continue
        off, ln = a, b
        o = off - 1
        if kind == "short":
            body += bytes([((o >> 3) & 0x60) | ((ln - 3) << 2), o & 0xFF])
        elif kind == "medium":
            body += bytes([0x80 | (ln - 4), o >> 8, o & 0xFF])
        else:  # long
            body += bytes([0xC0 | ((o >> 16) << 4) | (((ln - 5) >> 8) << 2), (o >> 8) & 0xFF,
                           o & 0xFF, (ln - 5) & 0xFF])
        for _ in range(ln):
            exp.append(exp[-off])
    body.append(0xFC)
    return bytes([0x10, 0xFB]) + len(exp).to_bytes(3, "big") + bytes(body), bytes(exp)


def test_qfs_back_references():
    stream, exp = _qfs_stream([
        ("lit", b"ABCDEFGH", 0),
        ("short", 4, 8),     # overlapping (off < ln)
        ("medium", 2, 6),    # overlapping
        ("long", 3, 10),     # overlapping
        ("medium", 20, 9),   # plain back-reference
        ("long", 5, 300),    # long length (uses high length bits)
    ])
    assert imp.unqfs(stream) == exp
    assert len(exp) > 300


def test_fsh_corrupt_raises_valueerror():
    raw = struct.pack("<4sII4s", b"SHPI", 16, 50, b"GIMX")  # claims 50 entries, none present
    bad_off = make_fsh([("chst", 0x78, 4, 4, bytes(32))])
    bad_off = bad_off[:16] + struct.pack("<4sI", b"chst", 0xFFFF) + bad_off[24:]
    for data in (raw, bad_off):
        with tempfile.TemporaryDirectory() as td:
            f = Path(td) / "c.fsh"
            f.write_bytes(data)
            try:
                imp.layers_from_fsh(f)
            except ValueError:
                continue
            raise AssertionError("expected ValueError")


def test_nearest_palette_black_gold():
    assert imp.PALETTE == GAME_PALETTE
    im = Image.new("RGBA", (10, 10), (0, 0, 0, 255))
    for x in range(10):
        for y in range(4):
            im.putpixel((x, y), (188, 144, 0, 255))
    got = imp.nearest_palette_colors(im)
    assert got == [4, 14], got


def test_nearest_palette_ignores_transparent():
    im = Image.new("RGBA", (4, 4), (255, 0, 0, 0))
    assert imp.nearest_palette_colors(im) == []


if __name__ == "__main__":
    for k, v in sorted(globals().items()):
        if k.startswith("test_"):
            v()
    print("ok")
