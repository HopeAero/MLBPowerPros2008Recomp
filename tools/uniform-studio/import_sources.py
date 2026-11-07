# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert user-supplied uniform sources into editable layers.

Supported inputs: EA SHPI ``.fsh`` (RefPack/QFS compressed or raw) and the MLB 2K
``.iff`` uniform container.  A layer is ``{"name", "rgba", "suggest"}`` where
``rgba`` is a PIL RGBA image and ``suggest`` a KIND (``body/chest``, ``cap``,
``number``) or ``""`` when unknown.  Nothing here ships game data; the files are
always supplied by the player.
"""
import io
import re
import struct

from PIL import Image

# --- RefPack / QFS ---------------------------------------------------------


def unqfs(d):
    """Decompress a RefPack (0x10FB) stream; return ``d`` unchanged if not one."""
    if len(d) < 5 or d[1] != 0xFB:
        return bytes(d)
    flags = d[0]
    p = 2
    if flags & 0x01:
        p += 3  # compressed-size field
    size = int.from_bytes(d[p:p + 3], "big")
    p += 3
    out = bytearray()
    while p < len(d):
        b0 = d[p]
        if b0 < 0x80:
            b1 = d[p + 1]
            p += 2
            lit = b0 & 3
            out += d[p:p + lit]
            p += lit
            ln = ((b0 & 0x1C) >> 2) + 3
            off = ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = d[p + 1], d[p + 2]
            p += 3
            lit = (b1 >> 6) & 3
            out += d[p:p + lit]
            p += lit
            ln = (b0 & 0x3F) + 4
            off = ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = d[p + 1], d[p + 2], d[p + 3]
            p += 4
            lit = b0 & 3
            out += d[p:p + lit]
            p += lit
            ln = ((b0 & 0x0C) << 6) + b3 + 5
            off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            lit = ((b0 & 0x1F) << 2) + 4
            p += 1
            out += d[p:p + lit]
            p += lit
            continue
        else:
            lit = b0 & 3
            p += 1
            out += d[p:p + lit]
            break
        s = len(out) - off
        if s < 0:
            raise ValueError("corrupt QFS stream")
        for k in range(ln):
            out.append(out[s + k])
    if len(out) != size:
        raise ValueError("QFS size mismatch (%d != %d)" % (len(out), size))
    return bytes(out)


# --- pixel decoders --------------------------------------------------------


def _dds(w, h, data, fourcc):
    bs = 8 if fourcc == b"DXT1" else 16
    n = max(1, w // 4) * max(1, h // 4) * bs
    if len(data) < n:
        raise ValueError("truncated %s data" % fourcc.decode())
    hdr = struct.pack("<4sIIIIIII44xII4sIIIIIIIII4x", b"DDS ", 124, 0x1007 | 0x80000,
                      h, w, w * h * bs // 16, 0, 1, 32, 4, fourcc, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0)
    return Image.open(io.BytesIO(hdr + data[:n])).convert("RGBA")


def _rgb565(w, h, px):
    out = bytearray()
    for (v,) in struct.iter_unpack("<H", px[:w * h * 2]):
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        out += bytes(((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31, 255))
    return Image.frombytes("RGBA", (w, h), bytes(out))


def _argb4444(w, h, px):
    out = bytearray()
    for (v,) in struct.iter_unpack("<H", px[:w * h * 2]):
        a, r, g, b = (v >> 12) & 15, (v >> 8) & 15, (v >> 4) & 15, v & 15
        out += bytes((r * 17, g * 17, b * 17, a * 17))
    return Image.frombytes("RGBA", (w, h), bytes(out))


def _decode_fsh_entry(t, w, h, px):
    if t == 0x78:
        return _rgb565(w, h, px)
    if t == 0x6D:
        return _argb4444(w, h, px)
    if t == 0x61:
        return _dds(w, h, px, b"DXT3")
    if t == 0x60:
        return _dds(w, h, px, b"DXT1")
    if t == 0x62:
        return _dds(w, h, px, b"DXT5")
    if t == 0x7D:
        return Image.frombuffer("RGBA", (w, h), bytes(px[:w * h * 4]), "raw", "BGRA", 0, 1)
    return None


# --- suggestions -----------------------------------------------------------

_FSH_SUGGEST = {"jerf": "body/chest", "jers": "body/chest", "jert": "body/chest",
                "chst": "body/chest", "hat": "cap"}


def suggest_for_name(name):
    name = name.strip().lower()
    if name in _FSH_SUGGEST:
        return _FSH_SUGGEST[name]
    if re.fullmatch(r"zig\d", name):
        return "number"
    return ""


# --- .fsh ------------------------------------------------------------------


def layers_from_fsh(path):
    with open(path, "rb") as f:
        d = unqfs(f.read())
    if d[:4] != b"SHPI":
        raise ValueError("not an SHPI .fsh file")
    layers = []
    try:
        _, _, n, _ = struct.unpack_from("<4sII4s", d, 0)
        if 16 + 8 * n > len(d):
            raise ValueError("corrupt .fsh directory")
        for i in range(n):
            raw_name, off = struct.unpack_from("<4sI", d, 16 + 8 * i)
            t = d[off]
            w, h = struct.unpack_from("<HH", d, off + 4)
            im = _decode_fsh_entry(t, w, h, d[off + 16:])
            if im is None:
                continue
            name = raw_name.decode("latin1").strip("\0 ")
            layers.append({"name": name, "rgba": im, "suggest": suggest_for_name(name)})
    except (struct.error, IndexError, OSError) as e:
        raise ValueError("corrupt .fsh file: %s" % e) from e
    return layers


# --- .iff (MLB 2K) ---------------------------------------------------------

IFF_MAGIC = 0xFF3BEF94
IFF_BLOCK0 = 0x208
IFF_TEX_HDR = 0xF0
# size-based hints for 2K uniform textures; unknown -> ""
_IFF_SUGGEST = {(512, 256): "body/chest", (128, 128): "cap"}


def layers_from_iff(path):
    """Decode the inline DXT textures (colour/AO masks) of a 2K uniform .iff."""
    with open(path, "rb") as f:
        d = f.read()
    if len(d) < IFF_BLOCK0 or struct.unpack_from("<I", d, 0)[0] != IFF_MAGIC:
        raise ValueError("not an MLB 2K .iff file")
    layers = []
    for m in re.finditer(rb"DXT[1-5]", d[IFF_BLOCK0:]):
        o = m.start() - 8
        if o < 0 or o % 4:
            continue
        b = d[IFF_BLOCK0 + o:IFF_BLOCK0 + o + IFF_TEX_HDR]
        if len(b) < IFF_TEX_HDR or b[28:32] != b[8:12]:
            continue
        w, h = struct.unpack_from("<HH", b, 0x68)
        flags = struct.unpack_from(">I", b, 0x64)[0]
        if (flags >> 16) != 0x021A or w < 4 or h < 4:
            continue  # normal maps keep their pixels in the second block
        fourcc = b[8:12]
        bs = 8 if fourcc == b"DXT1" else 16
        start = IFF_BLOCK0 + o + IFF_TEX_HDR
        if start + (w // 4) * (h // 4) * bs > len(d):
            continue
        try:
            im = _dds(w, h, d[start:], fourcc)
        except Exception:
            continue
        layers.append({"name": "tex_%06x" % o, "rgba": im,
                       "suggest": _IFF_SUGGEST.get((w, h), "")})
    return layers


# --- palette ---------------------------------------------------------------

# Mid-tone RGB of the game's 32 team colours (index = game colour id).
PALETTE = [
    (160, 160, 160), (110, 110, 110), (90, 90, 90), (65, 65, 65), (45, 45, 45), (37, 35, 88),
    (45, 43, 150), (43, 95, 130), (54, 120, 140), (0, 65, 50), (0, 80, 50), (0, 95, 55),
    (0, 120, 0), (165, 165, 80), (188, 144, 0), (185, 110, 0), (215, 130, 119), (181, 109, 74),
    (130, 50, 30), (200, 80, 0), (160, 45, 0), (140, 45, 0), (90, 30, 40), (205, 96, 127),
    (184, 53, 136), (120, 0, 150), (70, 0, 145), (114, 47, 225), (115, 75, 255), (0, 64, 66),
    (0, 98, 98), (55, 152, 135),
]


def _nearest(rgb, palette):
    r, g, b = rgb
    return min(range(len(palette)),
               key=lambda i: (palette[i][0] - r) ** 2 + (palette[i][1] - g) ** 2 + (palette[i][2] - b) ** 2)


def nearest_palette_colors(rgba, count=4, min_share=0.05, palette=None):
    """Dominant opaque colours of ``rgba`` as palette indices, most frequent first."""
    palette = palette or PALETTE
    im = rgba.convert("RGBA")
    if im.width * im.height > 65536:
        im.thumbnail((256, 256), Image.NEAREST)
    cache, hist, total = {}, {}, 0
    for r, g, b, a in im.getdata():
        if a < 128:
            continue
        key = (r >> 3, g >> 3, b >> 3)
        if key not in cache:
            cache[key] = _nearest((r, g, b), palette)
        hist[cache[key]] = hist.get(cache[key], 0) + 1
        total += 1
    if not total:
        return []
    ranked = sorted(hist.items(), key=lambda kv: (-kv[1], kv[0]))
    return [i for i, c in ranked if c / total >= min_share][:count]
