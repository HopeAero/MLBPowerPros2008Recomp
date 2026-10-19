# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a stadium_model.Stadium as its two packs, from empty buffers: nothing is read from an existing
pack. The layout follows docs/ingenieria-inversa.md, section 5 ("Estadios", "Inventario de campos para
escribir packs desde cero"): pointers are 0x40000000 | offset in their own file; display lists, vertex
arrays, texels and palette colours on 32 bytes, everything else on 4.
"""
import struct
from dataclasses import dataclass

from . import pj7m, stadium_tbl, tbl_write

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


# ---- table ---------------------------------------------------------------------------------------

MAX_TEXMAPS = 30            # most texmaps one piece has in the game; 82 overran a game table and crashed
MAX_PLACEMENTS = 0xFFF      # the runtime finds the root by a placement count below 0x1000
MAX_COLLISION = 0xFFFF      # u16 counts


def _tree_a(placements):
    """Tree A: one leaf in placement order when it fits (the game seems to draw pieces in the order it
    finds them here, as TblWriter.set_model_tree does), else a spatial tree."""
    boxes = [(i, tuple(p.bbox[:3]), tuple(p.bbox[3:])) for i, p in enumerate(placements)]
    if len(boxes) <= tbl_write.MAX_LEAF:
        far = max((b[2][0] for b in boxes), default=0.0) + 1.0
        n = len(boxes)
        return [((1.0, 0.0, 0.0, -far), (0, 0, n), (0, n, 0))], [i for i, _, _ in boxes]
    return tbl_write.build_tree(boxes)


def _tree_b(vertices, triangles):
    if not triangles:
        return [((1.0, 0.0, 0.0, 0.0), (0, 0, 0), (0, 0, 0))], []
    return tbl_write.build_tree(tbl_write.triangle_boxes(vertices, triangles), 12)


def write_table(st, layout):
    """The table (stadium_<code>tbl.pack) of `st`, pointing into the graphics pack laid out as `layout`."""
    buf = _Buffer(PACKWII)
    put = buf.put
    fields = {}
    for p in st.placements:
        put(struct.pack(">I19fI", P | layout.models[p.model], *p.translation, *p.rotation, *p.scale, *p.bbox,
                        *p.sphere, p.flags))
    fields["placements"] = 0x20
    for name, (nodes, entries) in (("a", _tree_a(st.placements)),
                                   ("b", _tree_b(st.collision_vertices, st.collision_triangles))):
        tree, lists = tbl_write.pack_tree(nodes, entries)
        fields["planes_" + name] = put(tree)
        fields["lists_" + name] = put(lists)
    fields["collision"] = put(struct.pack(">HHI", len(st.collision_triangles), len(st.collision_vertices), 0)
                              + b"".join(struct.pack(">4H", *t) for t in st.collision_triangles))
    fields["coll_vertices"] = put(b"".join(struct.pack(">3f", *v) for v in st.collision_vertices))
    groups = b""
    for g in st.unk_20:
        at = put(b"".join(struct.pack(">6f", *pt) for pt in g.points)) if g.points else None
        groups += struct.pack(">II6f", ptr(at), len(g.points), *g.box)
    fields["unk_20"] = put(groups + bytes(0x20))
    fields["unk_2c"] = put(struct.pack(f">I{len(st.preload_order)}I", len(st.preload_order), *st.preload_order))
    fields["textures"] = put(b"".join(
        struct.pack(">4I", ptr(layout.textures[e.texture] if e.texture is not None else None),
                    ptr(layout.palettes[e.palette] if e.palette is not None else None), e.tmem_a, e.tmem_b)
        for e in st.preload) + struct.pack(">4I", 0, 0, st.preload_tail, 0))
    pairs = b""
    for records in st.unk_24:
        at = put(b"".join(struct.pack(">7f", *r) for r in records)) if records else None
        pairs += struct.pack(">II", ptr(at), len(records))
    fields["unk_24"] = put(pairs)
    root = put(struct.pack(">I", len(st.placements))
               + b"".join(struct.pack(">I", P | fields[name]) for name in stadium_tbl.ROOT_FIELDS))
    put(b"", 32)
    if stadium_tbl.find_root(buf.d) != root:
        raise StadiumWriteError("the table root would not be found from the end of the file")
    return bytes(buf.d)


def check_limits(st):
    for m in st.models:
        if len(m.texmaps) > MAX_TEXMAPS:
            raise StadiumWriteError(f"{m.name}: {len(m.texmaps)} texmaps, the game takes {MAX_TEXMAPS}")
    if len(st.placements) > MAX_PLACEMENTS:
        raise StadiumWriteError(f"{len(st.placements)} placements, at most {MAX_PLACEMENTS}")
    if len(st.collision_triangles) > MAX_COLLISION or len(st.collision_vertices) > MAX_COLLISION:
        raise StadiumWriteError(f"collision of {len(st.collision_triangles)} triangles and {len(st.collision_vertices)} "
                                f"vertices, at most {MAX_COLLISION} of each")


def write_stadium(st):
    """(graphics pack, table) of `st`. Checks the known limits before writing anything."""
    check_limits(st)
    gfx, layout = write_graphics(st)
    try:
        tbl = write_table(st, layout)
    except tbl_write.TreeError as e:
        raise StadiumWriteError(str(e)) from e
    return gfx, tbl
