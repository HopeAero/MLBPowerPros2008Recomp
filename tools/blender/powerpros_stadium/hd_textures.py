# SPDX-License-Identifier: GPL-3.0-or-later
"""HD textures for exported stadiums, through the runtime's texture replacement.

The pack keeps a small RGB5A3 texture (at most 256 px, like the game's); the full image goes next to
the mod as mods/<mod>/textures/stadium_<code>/tex1_<w>x<h>_<xxh64>_5.png, the Dolphin-style name the
runtime looks up for that small texture. The runtime builds its mipmaps, so a 2K image stays clean
from afar. Without the runtime (or with replacements off) the small texture is what you see.
"""
import struct
import zlib

import numpy as np

MAX_HD = 2048           # longest side of an HD image; bigger ones are halved until they fit

_P1, _P2, _P3, _P4, _P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                          0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)
_M = (1 << 64) - 1


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _M


def _round(acc, lane):
    return (_rotl((acc + lane * _P2) & _M, 31) * _P1) & _M


def _merge(acc, val):
    return ((acc ^ _round(0, val)) * _P1 + _P4) & _M


def xxh64(data, seed=0):
    """XXH64, the hash the runtime (and Dolphin) puts in replacement texture names."""
    n = len(data)
    i = 0
    if n >= 32:
        v = [(seed + _P1 + _P2) & _M, (seed + _P2) & _M, seed, (seed - _P1) & _M]
        lanes = struct.unpack_from(f"<{(n // 32) * 4}Q", data)
        for k in range(0, len(lanes), 4):
            v[0] = _round(v[0], lanes[k])
            v[1] = _round(v[1], lanes[k + 1])
            v[2] = _round(v[2], lanes[k + 2])
            v[3] = _round(v[3], lanes[k + 3])
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & _M
        for x in v:
            h = _merge(h, x)
        i = (n // 32) * 32
    else:
        h = (seed + _P5) & _M
    h = (h + n) & _M
    while i + 8 <= n:
        h ^= _round(0, struct.unpack_from("<Q", data, i)[0])
        h = (_rotl(h, 27) * _P1 + _P4) & _M
        i += 8
    if i + 4 <= n:
        h ^= (struct.unpack_from("<I", data, i)[0] * _P1) & _M
        h = (_rotl(h, 23) * _P2 + _P3) & _M
        i += 4
    while i < n:
        h ^= (data[i] * _P5) & _M
        h = (_rotl(h, 11) * _P1) & _M
        i += 1
    h ^= h >> 33
    h = (h * _P2) & _M
    h ^= h >> 29
    h = (h * _P3) & _M
    return h ^ (h >> 32)


def replacement_name(texels, width, height, gx_format=5):
    """File name the runtime matches against a non-palette texture with these encoded texels."""
    return f"tex1_{width}x{height}_{xxh64(texels):016x}_{gx_format}.png"


def fit(rgba):
    """Halve an (h, w, 4) uint8 image (2x2 average) until its longest side is at most MAX_HD."""
    while max(rgba.shape[:2]) > MAX_HD:
        h, w = rgba.shape[0] // 2 * 2, rgba.shape[1] // 2 * 2
        r = rgba[:h, :w].astype(np.uint16)
        rgba = ((r[0::2, 0::2] + r[1::2, 0::2] + r[0::2, 1::2] + r[1::2, 1::2] + 2) // 4).astype(np.uint8)
    return rgba


def png_bytes(rgba):
    """An (h, w, 4) uint8 image, top row first, as an RGBA PNG."""
    h, w = rgba.shape[:2]
    raw = np.hstack([np.zeros((h, 1), np.uint8), np.ascontiguousarray(rgba).reshape(h, w * 4)]).tobytes()

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
