"""Uniform Studio baker: RGBA atlases -> C8 + RGB5A3 palette, and the .ppu container.

.ppu layout (reader in C++ must match exactly):
    magic     8 bytes   b"PPUNIF\\0\\0"
    version   u32 BE    1, or 2 when the flags byte is there
    checksum  u32 BE    FNV-1a 32-bit of every byte after this field
    backup    10 bytes  palette-of-32 indices (0..31), the record's 10 uniform bytes
    flags     u8        version 2 only; bit 0: backup bytes 8 / 9 (UNIFORM # / # OUTLINE colours) were
                        chosen in the editor, so the game takes them when the uniform is picked;
                        bit 1: a number layout follows
    layout    6 bytes   if flags bit 1: chest u8 (0 as the game, 1 none, 2..9 position 0..7),
                        s8 chest dx, s8 chest dy, s8 back dx, s8 back dy, u8 back size % (25..200)
    per atlas present, in order body, cap, helmet, number, thumb:
        kind-id u8      body=0, cap=1, helmet=2, number=3, thumb=4 (64x64 picture for the game's menu)
        w       u16 LE
        h       u16 LE
        palette 512 B   256 x RGB5A3, big-endian u16; entry 0 = 0x0000 (transparent)
        pixels  w*h B   C8 palette indices, GX-tiled (8x4 blocks)
"""
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import packwii  # noqa: E402

KIND = ("body", "cap", "helmet", "number", "thumb")
SIZE = {"body": (128, 128), "cap": (64, 32), "helmet": (64, 32), "number": (64, 32), "thumb": (64, 64)}
MAGIC = b"PPUNIF\0\0"
C8 = 9  # packwii GX format id for C8


def fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def bake_atlas(rgba, kind):
    """-> (palette512, tiled_c8). Entry 0 of the palette is transparent (0x0000)."""
    w, h = SIZE[kind]
    img = np.array(rgba.convert("RGBA"))
    if img.shape[:2] != (h, w):
        raise ValueError(f"{kind}: expected {w}x{h}, got {img.shape[1]}x{img.shape[0]}")
    img[img[..., 3] < 0x80] = 0  # clear pixels land on index 0
    pal = np.concatenate([np.zeros(1, np.uint16), packwii.new_palette(img, 255).astype(np.uint16)])
    pixels = packwii.encode(img, C8, pal)
    return pal.astype(">u2").tobytes(), pixels


def write_ppu(path, atlases, backup_colors, flags=0, layout=None):
    if len(backup_colors) != 10 or any(not 0 <= c <= 31 for c in backup_colors):
        raise ValueError("backup needs 10 indices in 0..31")
    if not 0 <= flags <= 1:
        raise ValueError("flags: only bit 0 is set by the caller")
    if layout is not None:
        chest, cdx, cdy, bdx, bdy, size = layout
        if not (0 <= chest <= 9 and all(-64 <= v <= 63 for v in (cdx, cdy, bdx, bdy)) and 25 <= size <= 200):
            raise ValueError("bad number layout")
        flags |= 2
    version = 2 if flags else 1
    body = bytes(backup_colors) + (bytes([flags]) if version == 2 else b"")
    if layout is not None:
        body += struct.pack("<BbbbbB", chest, cdx, cdy, bdx, bdy, size)
    for i, kind in enumerate(KIND):
        if kind not in atlases:
            continue
        pal, px = bake_atlas(atlases[kind], kind)
        w, h = SIZE[kind]
        body += struct.pack("<BHH", i, w, h) + pal + px
    Path(path).write_bytes(MAGIC + struct.pack(">II", version, fnv1a(body)) + body)


def read_ppu(path):
    d = Path(path).read_bytes()
    if len(d) < 16 or d[:8] != MAGIC:
        raise ValueError("bad magic")
    ver, ck = struct.unpack_from(">II", d, 8)
    if ver not in (1, 2):
        raise ValueError("bad version")
    body = d[16:]
    if len(body) < 10 or fnv1a(body) != ck:
        raise ValueError("bad checksum")
    backup = list(body[:10])
    if any(c > 31 for c in backup):
        raise ValueError("bad backup")
    flags = 0
    if ver == 2:
        if len(body) < 11 or body[10] > 3:
            raise ValueError("bad flags")
        flags = body[10]
    o, atlases = 10 + (ver == 2), {}
    layout = None
    if flags & 2:
        if len(body) < 17:
            raise ValueError("truncated")
        layout = struct.unpack_from("<BbbbbB", body, 11)
        if not (layout[0] <= 9 and 25 <= layout[5] <= 200):
            raise ValueError("bad number layout")
        o += 6
    while o < len(body):
        if o + 5 > len(body):
            raise ValueError("truncated")
        kid, w, h = struct.unpack_from("<BHH", body, o)
        o += 5
        if kid >= len(KIND) or KIND[kid] in atlases or (w, h) != SIZE[KIND[kid]]:
            raise ValueError("bad atlas header")
        if o + 512 + w * h > len(body):
            raise ValueError("truncated")
        atlases[KIND[kid]] = (w, h, body[o:o + 512], body[o + 512:o + 512 + w * h])
        o += 512 + w * h
    return {"version": ver, "atlases": atlases, "backup": backup, "flags": flags, "layout": layout}
