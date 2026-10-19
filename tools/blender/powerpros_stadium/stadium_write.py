# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a stadium_model.Stadium as its two packs, from empty buffers: nothing is read from an existing
pack. The layout follows docs/ingenieria-inversa.md, section 5 ("Estadios", "Inventario de campos para
escribir packs desde cero"): pointers are 0x40000000 | offset in their own file; display lists, vertex
arrays, texels and palette colours on 32 bytes, everything else on 4.
"""
import struct
from dataclasses import dataclass

from . import pj7m

P = 0x40000000
FMT_TYPE = {fmt: kind for kind, fmt in pj7m.MODEL_FMT.items()}      # GX format -> texture type byte
FMT_FLAGS = {8: 0x2000, 5: 0x6000}                                  # texture header +0x06 (0 otherwise)
PACKWII = b"PACKWII " + struct.pack(">6I", 0, 0, 0, 0x20, 0, 0)


class StadiumWriteError(ValueError):
    pass


@dataclass
class GraphicsLayout:
    models: list                   # offset of each model's PJ7M header
    textures: list                 # offset of each texture header
    palettes: list                 # offset of each palette header


class _Buffer:
    def __init__(self, head=b""):
        self.d = bytearray(head)

    def put(self, blob, align=4):
        self.d.extend(b"\0" * (-len(self.d) % align))
        off = len(self.d)
        self.d.extend(blob)
        return off

    def string(self, text):
        return self.put(text.encode("ascii") + b"\0")


def ptr(off):
    return 0 if off is None else P | off


def texture_header(tex, pixels_at):
    return (struct.pack(">HHBBHI", tex.height, tex.width, FMT_TYPE[tex.fmt], 0, FMT_FLAGS.get(tex.fmt, 0), P | pixels_at)
            + struct.pack(">4I", *[0x82000000] * 4) + bytes(8))


def palette_header(pal, colors_at):
    return struct.pack(">HHBBHI", pal.entries, 0, 0x81, 0, pal.format, ptr(colors_at))


def material_bytes(mat):
    slots = list(mat.texmaps) + [0xFFFF] * (8 - len(mat.texmaps))
    out = struct.pack(">HH", mat.kind, mat.texmaps[0] if mat.texmaps else 0xFFFF)
    out += bytes(c for rgba in mat.colors for c in rgba) + bytes(0x1C - 0x10)
    out += struct.pack(">8H", *slots) + bytes(0x32 - 0x2C) + struct.pack(">H", mat.unk_32)
    return out


def texmap_bytes(tm, tex):
    out = bytearray(0x4C)
    struct.pack_into(">HH", out, 0, tm.slot, 4)
    struct.pack_into(">HH", out, 0x0C, (tm.width or tex.width) - 1, (tm.height or tex.height) - 1)
    struct.pack_into(">HH", out, 0x14, 1, 1)
    struct.pack_into(">HH", out, 0x1C, tm.repeat_u, tm.repeat_v)
    struct.pack_into(">ff", out, 0x20, 0.75, 1.0)
    struct.pack_into(">ff", out, 0x38, 1.0, 1.0)
    struct.pack_into(">H", out, 0x40, 1)
    return bytes(out)


def display_list(draws):
    dl = bytearray()
    for op, verts in draws:
        dl += struct.pack(">BH", op, len(verts))
        for v in verts:
            dl += struct.pack(">BHHHH", *v)
    return bytes(dl) + b"\0" * (-len(dl) % 32)


def _textures(buf, st):
    """Every texture, its palette header at +0x24 of the first texture paired with it (as the game
    lays C4/C8 out), then the remaining palettes."""
    pairs = [(t, p) for m in st.models for t, p, _ in m.textures] + [(e.texture, e.palette) for e in st.preload]
    pairs += list(st.spare_textures)
    home = {}
    for t, p in pairs:
        if t is not None and p is not None:
            home.setdefault(t, p)
    tex_at, pal_at = [None] * len(st.textures), [None] * len(st.palettes)
    placed = set()
    for i, tex in enumerate(st.textures):
        head = buf.put(bytes(0x24), 32)                 # 0x1C of header, zero to +0x24
        p = home.get(i)
        if p is not None and p not in placed:
            pal_at[p] = buf.put(bytes(0x0C))            # at +0x24, as the game lays C4/C8 out
            placed.add(p)
        pix = buf.put(tex.pixels, 32)
        if tex.tail:
            buf.put(tex.tail, 1)
        buf.d[head:head + 0x24] = texture_header(tex, pix)
        tex_at[i] = head
    for p, pal in enumerate(st.palettes):
        if pal_at[p] is None:
            pal_at[p] = buf.put(bytes(0x0C))
        colors = buf.put(pal.colors, 32) if pal.colors else None
        buf.d[pal_at[p]:pal_at[p] + 0x0C] = palette_header(pal, colors)
    return tex_at, pal_at


def _model(buf, st, model, tex_at, pal_at):
    """Write one model; returns the offset of its PJ7M header."""
    put = buf.put
    word = put(struct.pack(">I", model.scene_word))
    scene_list = put(struct.pack(">I", P | word))
    texmaps = put(b"".join(texmap_bytes(tm, st.textures[model.textures[tm.slot][0]]) for tm in model.texmaps)) \
        if model.texmaps else None
    materials = put(b"".join(material_bytes(m) for m in model.materials))
    entries = b""
    for mesh in model.meshes:
        dl = display_list(mesh.draws)
        dl_at = put(dl, 32)
        pos = put(b"".join(struct.pack(">3f", *v) for v in mesh.positions), 32) if mesh.positions else None
        nrm = put(b"".join(struct.pack(">3f", *v) for v in mesh.normals), 32) if mesh.normals else None
        clr = put(bytes(c for v in mesh.colors for c in v), 32) if mesh.colors else None
        uvs = put(b"".join(struct.pack(">2f", *v) for v in mesh.uvs), 32) if mesh.uvs else None
        at = put(struct.pack(">7I", 0, len(dl), P | dl_at, ptr(pos), ptr(nrm), ptr(clr), ptr(uvs)))
        entries += struct.pack(">HHI", mesh.material, 1, P | at)
    mesh_list = put(entries) if entries else None
    node_name = buf.string(model.node_name)
    node = put(struct.pack(">HHII", len(model.meshes), 0, ptr(mesh_list), P | node_name))
    scene = put(struct.pack(">9I", 1, P | node, 1, P | scene_list, len(model.texmaps), ptr(texmaps),
                            len(model.materials), P | materials, 0x37))
    textab = paltab = None
    if model.textures:
        textab = put(b"".join(struct.pack(">II", P | tex_at[t], level) for t, _, level in model.textures) + bytes(8))
        paltab = put(b"".join(struct.pack(">II", ptr(pal_at[p] if p is not None else None), 0)
                              for _, p, _ in model.textures) + bytes(8))
    wname = buf.string(model.wname)
    root = put(struct.pack(">4I", 9, P | scene, ptr(textab), ptr(paltab)) + bytes(0x30) + struct.pack(">I", P | wname))
    name = buf.string(model.name)
    positions = sum(len(m.positions) for m in model.meshes)
    triangles = sum(len(v) - 2 if op == 0x98 else len(v) // 3 for m in model.meshes for op, v in m.draws)
    return put(b"PJ7M" + struct.pack(">9I", 0x28, P | name, P | root, 0, 0, len(model.textures), positions,
                                     triangles, 0))


def write_graphics(st):
    """The graphics pack (stadium_<code>.pack) of `st`, and where its models, textures and palettes are."""
    buf = _Buffer(PACKWII)
    tex_at, pal_at = _textures(buf, st)
    models = [_model(buf, st, m, tex_at, pal_at) for m in st.models]
    buf.put(b"", 32)
    return bytes(buf.d), GraphicsLayout(models, tex_at, pal_at)
